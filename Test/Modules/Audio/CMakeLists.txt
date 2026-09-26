add_test(NAME AudioModuleLifecycleTest COMMAND AudioBackendLifetimeTest --test=AudioModuleLifecycleTest)

get_target_property(audio_domain_links NorvesAudioDomain LINK_LIBRARIES)
string(JOIN ";" audio_domain_link_text ${audio_domain_links})
if(audio_domain_link_text MATCHES "XAudio|xaudio|NorvesAudioBackend_XAudio2|NorvesModule_Audio")
    message(FATAL_ERROR "NorvesAudioDomain must not link a platform audio backend")
endif()

file(GLOB_RECURSE audio_backend_neutral_sources
    "${CMAKE_SOURCE_DIR}/Library/Modules/Audio/Public/*"
    "${CMAKE_SOURCE_DIR}/Library/Modules/Audio/Private/Audio/AudioBackend.h"
    "${CMAKE_SOURCE_DIR}/Library/Modules/Audio/Private/Audio/AudioClipResource.cpp"
    "${CMAKE_SOURCE_DIR}/Library/Modules/Audio/Private/Audio/AudioDomain.cpp"
    "${CMAKE_SOURCE_DIR}/Library/Modules/Audio/Private/Audio/AudioDomain.h"
)
foreach(audio_source IN LISTS audio_backend_neutral_sources)
    file(READ "${audio_source}" audio_source_text)
    if(audio_source_text MATCHES "#include[ \t]*[<\"](Windows\\.h|xaudio2\\.h)[>\"]|XAudio2(Create|Voice|Backend)")
        message(FATAL_ERROR "backend-specific audio dependency escaped adapter: ${audio_source}")
    endif()
endforeach()


norves_add_test_bundle(AudioBackendLifetimeTest
    FOLDER "Test/Modules/Audio"
    MEMBER AudioBackendLifetimeTest AudioBackendLifetimeTest.cpp
    MEMBER AudioModuleLifecycleTest AudioModuleLifecycleTest.cpp
    MEMBER AudioModuleRegistryTest AudioModuleRegistryTest.cpp
)
target_link_libraries(AudioBackendLifetimeTest PRIVATE Core NorvesAudioDomain)
target_include_directories(AudioBackendLifetimeTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Library/Modules/Audio/Private
)
add_test(NAME AudioBackendLifetimeTest COMMAND AudioBackendLifetimeTest)

get_target_property(audio_lifetime_links AudioBackendLifetimeTest LINK_LIBRARIES)
string(JOIN ";" audio_lifetime_link_text ${audio_lifetime_links})
if(audio_lifetime_link_text MATCHES "XAudio|xaudio|NorvesAudioBackend_XAudio2|NorvesModule_Audio")
    message(FATAL_ERROR "AudioBackendLifetimeTest must remain fake-only")
endif()

add_test(NAME AudioModuleRegistryTest COMMAND AudioBackendLifetimeTest --test=AudioModuleRegistryTest)

