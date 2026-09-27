cmake_minimum_required(VERSION 3.21)
project(GuitarChordPlayer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Multimedia)
qt_standard_project_setup()

qt_add_executable(GuitarChordPlayer WIN32
    src/main.cpp
    src/ChordLibrary.h src/ChordLibrary.cpp
    src/Song.h src/Song.cpp
    src/GuitarSynth.h src/GuitarSynth.cpp
    src/Sequencer.h src/Sequencer.cpp
    src/AudioEngine.h src/AudioEngine.cpp
    src/WavWriter.h src/WavWriter.cpp
    src/ChordDiagramWidget.h src/ChordDiagramWidget.cpp
    src/PatternWidget.h src/PatternWidget.cpp
    src/MainWindow.h src/MainWindow.cpp
    src/ChordSheet.h src/ChordSheet.cpp
    src/NewSongDialog.h src/NewSongDialog.cpp
    src/SongEditor.h src/SongEditor.cpp
    src/ChordFinderDialog.h src/ChordFinderDialog.cpp
)

target_link_libraries(GuitarChordPlayer PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Multimedia)

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
