set(AUDIO_PUBLIC_HEADERS
    Public/Audio/AudioClipResource.h
    Public/Audio/AudioComponent.h
    Public/Audio/AudioGameModeOwner.h
    Public/Audio/AudioTypes.h
    Public/Audio/IAudioModule.h
    Public/Audio/IAudioService.h
    Public/Audio/ISoundSystem.h
    Public/Audio/SoundBankTypes.h
)

add_library(NorvesAudioDomain STATIC
    ${AUDIO_PUBLIC_HEADERS}
    Private/Audio/AudioBackend.h
    Private/Audio/NullAudioBackend.h
    Private/Audio/NullAudioBackend.cpp
    Private/Audio/AudioClipResource.cpp
    Private/Audio/AudioComponent.cpp
    Private/Audio/AudioGameModeOwner.cpp
    Private/Audio/AudioDomain.cpp
    Private/Audio/AudioDomain.h
    Private/Audio/AudioServiceModule.cpp
    Private/Audio/SoundBank.h
    Private/Audio/SoundBank.cpp
    Private/Audio/AudioClipLoader.h
    Private/Audio/AudioClipLoader.cpp
    Private/Audio/SoundSystem.h
    Private/Audio/SoundSystem.cpp
)
target_link_libraries(NorvesAudioDomain PUBLIC Core)
target_include_directories(NorvesAudioDomain
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Public
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Private
)
target_compile_features(NorvesAudioDomain PRIVATE cxx_std_23)

add_library(NorvesAudioBackend_XAudio2 STATIC
    Private/Audio/AudioBackendFactory.h
    Private/Audio/XAudio2Backend.cpp
    Private/Audio/XAudio2BackendFactory.h
)
target_link_libraries(NorvesAudioBackend_XAudio2 PRIVATE Core NorvesAudioDomain xaudio2_8)
target_include_directories(NorvesAudioBackend_XAudio2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Private)
target_compile_features(NorvesAudioBackend_XAudio2 PRIVATE cxx_std_23)

add_library(NorvesModule_Audio STATIC
    Private/Audio/AudioModule.cpp
    ${AUDIO_PUBLIC_HEADERS}
)
target_link_libraries(NorvesModule_Audio
    PUBLIC NorvesAudioDomain
    PRIVATE NorvesAudioBackend_XAudio2
)
target_include_directories(NorvesModule_Audio
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Public
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Private
)
target_compile_features(NorvesModule_Audio PRIVATE cxx_std_23)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
    ${AUDIO_PUBLIC_HEADERS}
    Private/Audio/AudioBackend.h
    Private/Audio/NullAudioBackend.h
    Private/Audio/NullAudioBackend.cpp
    Private/Audio/AudioBackendFactory.h
    Private/Audio/AudioClipResource.cpp
    Private/Audio/AudioComponent.cpp
    Private/Audio/AudioGameModeOwner.cpp
    Private/Audio/AudioDomain.cpp
    Private/Audio/AudioDomain.h
    Private/Audio/AudioModule.cpp
    Private/Audio/AudioServiceModule.cpp
    Private/Audio/SoundBank.h
    Private/Audio/SoundBank.cpp
    Private/Audio/AudioClipLoader.h
    Private/Audio/AudioClipLoader.cpp
    Private/Audio/SoundSystem.h
    Private/Audio/SoundSystem.cpp
    Private/Audio/XAudio2Backend.cpp
    Private/Audio/XAudio2BackendFactory.h
)
