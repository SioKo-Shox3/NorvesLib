add_executable(AudioModuleLifecycleTest AudioModuleLifecycleTest.cpp)
target_link_libraries(AudioModuleLifecycleTest PRIVATE Core NorvesAudioDomain)
target_include_directories(AudioModuleLifecycleTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Library/Modules/Audio/Private
)
set_target_properties(AudioModuleLifecycleTest PROPERTIES FOLDER "Test/Modules/Audio")
add_test(NAME AudioModuleLifecycleTest COMMAND AudioModuleLifecycleTest)

get_target_property(audio_domain_links NorvesAudioDomain LINK_LIBRARIES)
string(JOIN ";" audio_domain_link_text ${audio_domain_links})
if(audio_domain_link_text MATCHES "XAudio|xaudio|NorvesAudioBackend_XAudio2|NorvesModule_Audio")
    message(FATAL_ERROR "NorvesAudioDomain must not link a platform audio backend")
endif()

get_target_property(audio_lifecycle_links AudioModuleLifecycleTest LINK_LIBRARIES)
string(JOIN ";" audio_lifecycle_link_text ${audio_lifecycle_links})
if(audio_lifecycle_link_text MATCHES "XAudio|xaudio|NorvesAudioBackend_XAudio2|NorvesModule_Audio")
    message(FATAL_ERROR "AudioModuleLifecycleTest must remain fake-only")
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

add_executable(AudioBackendLifetimeTest AudioBackendLifetimeTest.cpp)
target_link_libraries(AudioBackendLifetimeTest PRIVATE Core NorvesAudioDomain)
target_include_directories(AudioBackendLifetimeTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Library/Modules/Audio/Private
)
set_target_properties(AudioBackendLifetimeTest PROPERTIES FOLDER "Test/Modules/Audio")
add_test(NAME AudioBackendLifetimeTest COMMAND AudioBackendLifetimeTest)

get_target_property(audio_lifetime_links AudioBackendLifetimeTest LINK_LIBRARIES)
string(JOIN ";" audio_lifetime_link_text ${audio_lifetime_links})
if(audio_lifetime_link_text MATCHES "XAudio|xaudio|NorvesAudioBackend_XAudio2|NorvesModule_Audio")
    message(FATAL_ERROR "AudioBackendLifetimeTest must remain fake-only")
endif()

add_executable(AudioDefaultFactoryTest AudioDefaultFactoryTest.cpp)
target_link_libraries(AudioDefaultFactoryTest PRIVATE NorvesModule_Audio)
set_target_properties(AudioDefaultFactoryTest PROPERTIES FOLDER "Test/Modules/Audio")
add_test(NAME AudioDefaultFactoryTest COMMAND AudioDefaultFactoryTest)

add_executable(AudioModuleRegistryTest AudioModuleRegistryTest.cpp)
target_link_libraries(AudioModuleRegistryTest PRIVATE Core NorvesAudioDomain)
target_include_directories(AudioModuleRegistryTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Library/Modules/Audio/Private
)
set_target_properties(AudioModuleRegistryTest PROPERTIES FOLDER "Test/Modules/Audio")
add_test(NAME AudioModuleRegistryTest COMMAND AudioModuleRegistryTest)

add_executable(AudioXAudio2AdapterContractTest AudioXAudio2AdapterContractTest.cpp)
target_link_libraries(AudioXAudio2AdapterContractTest PRIVATE Core)
target_compile_definitions(AudioXAudio2AdapterContractTest PRIVATE
    AUDIO_XAUDIO2_SOURCE_PATH="${CMAKE_SOURCE_DIR}/Library/Modules/Audio/Private/Audio/XAudio2Backend.cpp"
    AUDIO_DOMAIN_SOURCE_PATH="${CMAKE_SOURCE_DIR}/Library/Modules/Audio/Private/Audio/AudioDomain.cpp"
)
set_target_properties(AudioXAudio2AdapterContractTest PROPERTIES FOLDER "Test/Modules/Audio")
add_test(NAME AudioXAudio2AdapterContractTest COMMAND AudioXAudio2AdapterContractTest)
