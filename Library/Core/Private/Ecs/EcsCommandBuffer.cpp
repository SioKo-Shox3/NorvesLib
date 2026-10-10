#include "Ecs/EcsCommandBuffer.h"

#include <cstring>

namespace NorvesLib::Core::Ecs
{
    namespace
    {
        /// Create の値の並びの 1 項目の見出し（直後に Size byte の値が続く）
        struct PayloadValueHeader
        {
            TypeId Type = 0;
            uint32_t Size = 0;
        };
    }

    EcsCommandBuffer::EcsCommandBuffer(uint32_t bufferId)
        : m_BufferId(bufferId)
    {
    }

    uint32_t EcsCommandBuffer::AppendPayload(const void *data, uint32_t size)
    {
        const uint32_t offset = static_cast<uint32_t>(m_Payload.size());
        if (size > 0)
        {
            m_Payload.resize(m_Payload.size() + size);
            std::memcpy(m_Payload.data() + offset, data, size);
        }
        return offset;
    }

    bool EcsCommandBuffer::OwnsTicket(const EcsCreateTicket &ticket) const
    {
        return ticket.IsValid() && ticket.Buffer == m_BufferId && ticket.Generation == m_Generation
            && ticket.Index < m_Creates.size();
    }

    EcsCommandBuffer::Command EcsCommandBuffer::MakeCommand(Kind kind, const EcsCommandTarget &target) const
    {
        Command command;
        command.CommandKind = kind;
        if (target.Ticket.IsValid())
        {
            // 別のバッファの札・Clear 前の札・範囲外の札は、対象なし（再生で捨てる）
            if (OwnsTicket(target.Ticket))
            {
                command.TargetCreate = target.Ticket.Index;
            }
        }
        else
        {
            command.Entity = target.Entity;
        }
        return command;
    }

    EcsCreateTicket EcsCommandBuffer::Create(EcsArchetypeId archetype, Container::Span<const EcsColumnValue> initialValues)
    {
        Command command;
        command.CommandKind = Kind::Create;
        command.Archetype = archetype;
        command.CreateIndex = static_cast<uint32_t>(m_Creates.size());
        command.PayloadOffset = static_cast<uint32_t>(m_Payload.size());
        for (const EcsColumnValue &value : initialValues)
        {
            if (value.Data == nullptr || value.Size == 0)
            {
                continue;
            }
            PayloadValueHeader header;
            header.Type = value.Type;
            header.Size = value.Size;
            AppendPayload(&header, sizeof(header));
            AppendPayload(value.Data, value.Size);
            ++command.ValueCount;
        }
        m_Creates.push_back(CreateRecord{});
        m_Commands.push_back(command);

        EcsCreateTicket ticket;
        ticket.Buffer = m_BufferId;
        ticket.Index = command.CreateIndex;
        ticket.Generation = m_Generation;
        return ticket;
    }

    void EcsCommandBuffer::Destroy(EcsCommandTarget target)
    {
        m_Commands.push_back(MakeCommand(Kind::Destroy, target));
    }

    void EcsCommandBuffer::AddColumn(EcsCommandTarget target, const EcsColumnInfo &column, const void *value)
    {
        Command command = MakeCommand(Kind::AddColumn, target);
        command.Type = column.Type;
        command.Size = column.Size;
        command.Align = column.Align;
        if (value != nullptr)
        {
            command.bHasValue = true;
            command.PayloadOffset = AppendPayload(value, column.Size);
        }
        m_Commands.push_back(command);
    }

    void EcsCommandBuffer::RemoveColumn(EcsCommandTarget target, TypeId type)
    {
        Command command = MakeCommand(Kind::RemoveColumn, target);
        command.Type = type;
        m_Commands.push_back(command);
    }

    void EcsCommandBuffer::Write(EcsCommandTarget target, TypeId type, const void *value, uint32_t size)
    {
        Command command = MakeCommand(Kind::Write, target);
        command.Type = type;
        command.Size = size;
        if (value != nullptr)
        {
            command.bHasValue = true;
            command.PayloadOffset = AppendPayload(value, size);
        }
        m_Commands.push_back(command);
    }

    bool EcsCommandBuffer::CancelCreate(EcsCreateTicket ticket)
    {
        if (m_bReplayed || !OwnsTicket(ticket))
        {
            return false;
        }
        CreateRecord &record = m_Creates[ticket.Index];
        if (record.bCancelled)
        {
            return false;
        }
        record.bCancelled = true;
        return true;
    }

    EcsEntity EcsCommandBuffer::ResolveCreate(EcsCreateTicket ticket) const
    {
        if (!m_bReplayed || !OwnsTicket(ticket))
        {
            return kNullEcsEntity;
        }
        return m_Creates[ticket.Index].Entity;
    }

    void EcsCommandBuffer::Clear()
    {
        m_Commands.clear();
        m_Creates.clear();
        m_Payload.clear();
        ++m_Generation;
        m_bReplayed = false;
    }

    void EcsCommandBuffer::Apply(EcsWorld &world, const Command &command, EcsReplayResult &result)
    {
        if (command.CommandKind == Kind::Create)
        {
            CreateRecord &record = m_Creates[command.CreateIndex];
            if (record.bCancelled)
            {
                ++result.CancelledCreates;
                return;
            }
            Container::VariableArray<EcsColumnValue> values;
            size_t cursor = command.PayloadOffset;
            for (uint32_t i = 0; i < command.ValueCount; ++i)
            {
                PayloadValueHeader header;
                std::memcpy(&header, m_Payload.data() + cursor, sizeof(header));
                cursor += sizeof(header);
                EcsColumnValue value;
                value.Type = header.Type;
                value.Data = m_Payload.data() + cursor;
                value.Size = header.Size;
                values.push_back(value);
                cursor += header.Size;
            }
            record.Entity = world.CreateEntity(command.Archetype, Container::Span<const EcsColumnValue>(values.data(), values.size()));
            if (record.Entity.IsNull())
            {
                ++result.Rejected;
                return;
            }
            ++result.Created;
            ++result.Applied;
            return;
        }

        const EcsEntity target = command.TargetCreate != kNone ? m_Creates[command.TargetCreate].Entity : command.Entity;
        if (!world.IsAlive(target))
        {
            ++result.DiscardedStale;
            return;
        }

        bool applied = false;
        switch (command.CommandKind)
        {
        case Kind::Destroy:
            applied = world.DestroyEntity(target);
            break;
        case Kind::AddColumn:
        {
            EcsColumnInfo column;
            column.Type = command.Type;
            column.Size = command.Size;
            column.Align = command.Align;
            applied = world.AddColumn(target, column, command.bHasValue ? m_Payload.data() + command.PayloadOffset : nullptr);
            break;
        }
        case Kind::RemoveColumn:
            applied = world.RemoveColumn(target, command.Type);
            break;
        case Kind::Write:
        {
            // 書く大きさが列と食い違うなら、読み過ぎないように拒む
            const EcsArchetypeId archetype = world.GetArchetypeOf(target);
            const int32_t column = world.FindColumnIndex(archetype, command.Type);
            if (command.bHasValue && column >= 0 && world.GetArchetypeColumns(archetype)[column].Size == command.Size)
            {
                applied = world.WriteColumnValue(target, command.Type, m_Payload.data() + command.PayloadOffset);
            }
            break;
        }
        case Kind::Create:
            break;
        }
        if (applied)
        {
            ++result.Applied;
        }
        else
        {
            ++result.Rejected;
        }
    }

    EcsReplayResult EcsCommandBuffer::Replay(EcsWorld &world, Container::Span<EcsCommandBuffer *const> buffersInJobOrder)
    {
        EcsReplayResult result;
        bool hasCommands = false;
        for (const EcsCommandBuffer *buffer : buffersInJobOrder)
        {
            if (buffer != nullptr && !buffer->m_bReplayed && !buffer->m_Commands.empty())
            {
                hasCommands = true;
                break;
            }
        }
        if (!hasCommands)
        {
            return result;
        }
        world.AdvanceVersion();
        for (EcsCommandBuffer *buffer : buffersInJobOrder)
        {
            if (buffer == nullptr || buffer->m_bReplayed)
            {
                continue;
            }
            for (const Command &command : buffer->m_Commands)
            {
                buffer->Apply(world, command, result);
            }
            buffer->m_bReplayed = true;
        }
        return result;
    }

}
