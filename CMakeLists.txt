cmake_minimum_required(VERSION 3.21)
project(GuitarChordPlayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Multimedia Network)
qt_standard_project_setup()

qt_add_executable(GuitarChordPlayer WIN32
    src/main.cpp
    src/ChordLibrary.h src/ChordLibrary.cpp
    src/ChordName.h src/ChordName.cpp
    src/Song.h src/Song.cpp
    src/GuitarSynth.h src/GuitarSynth.cpp
    src/Sequencer.h src/Sequencer.cpp
    src/AudioEngine.h src/AudioEngine.cpp
    src/WavWriter.h src/WavWriter.cpp
    src/PdfExport.h src/PdfExport.cpp
    src/AudioTrack.h src/AudioTrack.cpp
    third_party/minimp3/minimp3_impl.cpp
    src/ChordDetector.h src/ChordDetector.cpp
    src/Arranger.h src/Arranger.cpp
    src/Translator.h src/Translator.cpp
    src/LyricsAligner.h src/LyricsAligner.cpp
    src/LyricsDialog.h src/LyricsDialog.cpp
    src/ChordDiagramWidget.h src/ChordDiagramWidget.cpp
    src/PatternWidget.h src/PatternWidget.cpp
    src/LyricsWidget.h src/LyricsWidget.cpp
    src/MainWindow.h src/MainWindow.cpp
    src/ChordSheet.h src/ChordSheet.cpp
    src/NewSongDialog.h src/NewSongDialog.cpp
    src/SongEditor.h src/SongEditor.cpp
    src/ChordFinderDialog.h src/ChordFinderDialog.cpp
    src/LlmPrompt.h src/LlmPrompt.cpp
    src/Updater.h src/Updater.cpp
)

# The LLM prompt is kept in docs/ and embedded so the app can copy it to the clipboard.
qt_add_resources(GuitarChordPlayer docs
    PREFIX "/"
    FILES docs/LLM_PROMPT.md resources/app.png resources/i18n/ar.json
)

target_include_directories(GuitarChordPlayer PRIVATE third_party/minimp3)
# minimp3 must see the same sample type everywhere it is included.
target_compile_definitions(GuitarChordPlayer PRIVATE MINIMP3_FLOAT_OUTPUT)
target_link_libraries(GuitarChordPlayer PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Multimedia Qt6::Network)

# The updater compares APP_BUILD with the newest GitHub release ("build-<number>").
# CI sets APP_BUILD to the workflow run number; local builds are 0 (development).
set(APP_BUILD 0 CACHE STRING "Build number, set by CI")
set(UPDATE_REPO "bahalouati/GuitarChordPlayer" CACHE STRING "GitHub repository to update from")
target_compile_definitions(GuitarChordPlayer PRIVATE APP_BUILD=${APP_BUILD} UPDATE_REPO="${UPDATE_REPO}"
                                                     APP_VERSION="${PROJECT_VERSION}")

if(WIN32)
    # Executable icon and version information shown in Explorer.
    configure_file(resources/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
    target_sources(GuitarChordPlayer PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
endif()

if(MSVC)
    target_compile_options(GuitarChordPlayer PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(GuitarChordPlayer PRIVATE -Wall -Wextra)
endif()

# Copy the example songs next to the executable so the app finds them.
add_custom_command(TARGET GuitarChordPlayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/songs
            $<TARGET_FILE_DIR:GuitarChordPlayer>/songs)

install(TARGETS GuitarChordPlayer RUNTIME DESTINATION .)
install(DIRECTORY songs DESTINATION .)
