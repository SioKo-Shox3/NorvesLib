// InputRouterTest — 優先度付きイベント配送ルーターの純ロジック単体テスト。
// Engine/RHI/描画に非依存。FakeController で各 On* の consume 可否と受信を制御・記録する。
//
// 検証観点:
//  ① 誰も consume しない → 全 Controller に透過配送される。
//  ② 高優先が consume(true) → 低優先には届かない。
//  ③ 高優先が当該イベント型のみ consume・他型は低優先へ届く。
//  ④ 同 priority 複数で「自分の対象のみ consume・他は false」→ 後続も受信する。
//  ⑤ Unregister 後は届かない／Register 後は届く（冪等性含む）。
//  ⑥ 優先度降順（高→低）の配送順を受信記録で検証する。

#include "Input/InputRouter.h"
#include "Input/IInputController.h"
#include "Input/InputTypes.h"
#include "CoreTypes.h"

#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Input;

namespace
{
    // 全 Controller 共有の配送順ログ（受信した DebugName を時系列で刻む）。
    Container::VariableArray<Container::String> g_DispatchOrder;

    // テスト用コントローラ。各イベント型ごとに consume するか（bool メンバ）と
    // 受信回数を制御・記録する。受信時に g_DispatchOrder へ名前を刻む。
    class FakeController : public IInputController
    {
    public:
        explicit FakeController(const char *name)
            : m_Name(name)
        {
        }

        bool OnMouseButton(const MouseButtonEvent &) override
        {
            ++ReceivedMouseButton;
            g_DispatchOrder.push_back(Container::String(m_Name));
            return bConsumeMouseButton;
        }

        bool OnMouseMove(const MouseMoveEvent &) override
        {
            ++ReceivedMouseMove;
            g_DispatchOrder.push_back(Container::String(m_Name));
            return bConsumeMouseMove;
        }

        bool OnMouseScroll(const MouseScrollEvent &) override
        {
            ++ReceivedMouseScroll;
            g_DispatchOrder.push_back(Container::String(m_Name));
            return bConsumeMouseScroll;
        }

        bool OnKey(const KeyEvent &) override
        {
            ++ReceivedKey;
            g_DispatchOrder.push_back(Container::String(m_Name));
            return bConsumeKey;
        }

        bool OnChar(const CharEvent &) override
        {
            ++ReceivedChar;
            g_DispatchOrder.push_back(Container::String(m_Name));
            return bConsumeChar;
        }

        const char *DebugName() const override
        {
            return m_Name;
        }

        // consume 制御（true でそのイベント型を打ち切る）
        bool bConsumeMouseButton = false;
        bool bConsumeMouseMove = false;
        bool bConsumeMouseScroll = false;
        bool bConsumeKey = false;
        bool bConsumeChar = false;

        // 受信回数
        int ReceivedMouseButton = 0;
        int ReceivedMouseMove = 0;
        int ReceivedMouseScroll = 0;
        int ReceivedKey = 0;
        int ReceivedChar = 0;

    private:
        const char *m_Name;
    };

    KeyEvent MakeKey()
    {
        KeyEvent e;
        e.Code = KeyCode::A;
        e.Action = InputAction::Pressed;
        return e;
    }

    MouseButtonEvent MakeButton()
    {
        MouseButtonEvent e;
        e.Button = MouseButton::Left;
        e.Action = InputAction::Pressed;
        return e;
    }

    // ① 誰も consume しない → 全 Controller が受信（透過）。
    void TestPassThrough()
    {
        std::cout << "[Test] PassThrough (誰も consume しない → 全受信)\n";

        InputRouter router;
        FakeController a("A");
        FakeController b("B");
        FakeController c("C");
        router.RegisterController(&a, 30);
        router.RegisterController(&b, 20);
        router.RegisterController(&c, 10);

        router.DispatchKey(MakeKey());

        assert(a.ReceivedKey == 1);
        assert(b.ReceivedKey == 1);
        assert(c.ReceivedKey == 1);
        std::cout << "  OK: 全 Controller が受信した\n";
    }

    // ② 高優先が consume(true) → 低優先に届かない。
    void TestConsumeStopsLowerPriority()
    {
        std::cout << "[Test] ConsumeStopsLowerPriority (高優先 consume → 低優先に届かない)\n";

        InputRouter router;
        FakeController high("High");
        FakeController low("Low");
        high.bConsumeKey = true;
        router.RegisterController(&high, InputRouter::PriorityOverlay);
        router.RegisterController(&low, InputRouter::PriorityGame);

        router.DispatchKey(MakeKey());

        assert(high.ReceivedKey == 1);
        assert(low.ReceivedKey == 0);
        std::cout << "  OK: 高優先で打ち切られ低優先に届かなかった\n";
    }

    // ③ 高優先が当該イベント型のみ consume・他型は低優先へ届く。
    void TestPerEventTypeConsume()
    {
        std::cout << "[Test] PerEventTypeConsume (型ごとの consume 独立性)\n";

        InputRouter router;
        FakeController high("High");
        FakeController low("Low");
        // Key は consume するが MouseButton は consume しない。
        high.bConsumeKey = true;
        high.bConsumeMouseButton = false;
        router.RegisterController(&high, InputRouter::PriorityOverlay);
        router.RegisterController(&low, InputRouter::PriorityGame);

        router.DispatchKey(MakeKey());
        router.DispatchMouseButton(MakeButton());

        // Key は low に届かない。MouseButton は low に届く。
        assert(high.ReceivedKey == 1);
        assert(low.ReceivedKey == 0);
        assert(high.ReceivedMouseButton == 1);
        assert(low.ReceivedMouseButton == 1);
        std::cout << "  OK: Key は遮断・MouseButton は透過した\n";
    }

    // ④ 同 priority 複数で「自分の対象のみ consume・他は false」→ 後続も受信。
    void TestSamePriorityStable()
    {
        std::cout << "[Test] SamePriorityStable (同優先・false 伝播は後続も受信)\n";

        InputRouter router;
        FakeController first("First");
        FakeController second("Second");
        // どちらも Key を consume しない（false）ので両方受信する。
        router.RegisterController(&first, InputRouter::PriorityGame);
        router.RegisterController(&second, InputRouter::PriorityGame);

        router.DispatchKey(MakeKey());

        assert(first.ReceivedKey == 1);
        assert(second.ReceivedKey == 1);
        // 同優先は登録順安定（First → Second）。
        assert(g_DispatchOrder.size() == 2);
        assert(g_DispatchOrder[0] == Container::String("First"));
        assert(g_DispatchOrder[1] == Container::String("Second"));
        std::cout << "  OK: 同優先で両方受信・登録順に配送された\n";
    }

    // ⑤ Unregister 後は届かない／Register 後は届く（冪等性含む）。
    void TestRegisterUnregister()
    {
        std::cout << "[Test] RegisterUnregister (登録/解除/冪等)\n";

        InputRouter router;
        FakeController a("A");

        // 登録前は届かない。
        router.DispatchKey(MakeKey());
        assert(a.ReceivedKey == 0);

        // 登録後は届く。
        router.RegisterController(&a, InputRouter::PriorityGame);
        router.DispatchKey(MakeKey());
        assert(a.ReceivedKey == 1);

        // 解除後は届かない。
        router.UnregisterController(&a);
        router.DispatchKey(MakeKey());
        assert(a.ReceivedKey == 1);

        // 二重解除は no-op（冪等・クラッシュしない）。
        router.UnregisterController(&a);
        router.UnregisterController(nullptr);
        router.DispatchKey(MakeKey());
        assert(a.ReceivedKey == 1);
        std::cout << "  OK: 登録/解除/冪等が期待通り\n";
    }

    // ⑥ 優先度降順（高→低）の配送順を受信記録で検証する。
    void TestPriorityDescendingOrder()
    {
        std::cout << "[Test] PriorityDescendingOrder (高→低の配送順)\n";

        InputRouter router;
        FakeController low("Low");
        FakeController mid("Mid");
        FakeController high("High");

        // 登録順を優先度とバラバラにして、配送順が priority 降順になることを示す。
        router.RegisterController(&low, 10);
        router.RegisterController(&high, 100);
        router.RegisterController(&mid, 50);

        router.DispatchKey(MakeKey());

        assert(g_DispatchOrder.size() == 3);
        assert(g_DispatchOrder[0] == Container::String("High"));
        assert(g_DispatchOrder[1] == Container::String("Mid"));
        assert(g_DispatchOrder[2] == Container::String("Low"));
        std::cout << "  OK: High → Mid → Low の順に配送された\n";
    }
} // namespace

int main()
{
    std::cout << "=== InputRouterTest 開始 ===\n";

    g_DispatchOrder.clear();
    TestPassThrough();

    g_DispatchOrder.clear();
    TestConsumeStopsLowerPriority();

    g_DispatchOrder.clear();
    TestPerEventTypeConsume();

    g_DispatchOrder.clear();
    TestSamePriorityStable();

    g_DispatchOrder.clear();
    TestRegisterUnregister();

    g_DispatchOrder.clear();
    TestPriorityDescendingOrder();

    std::cout << "=== InputRouterTest 全テスト成功 ===\n";
    return 0;
}
