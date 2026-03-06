#include <catch2/catch_test_macros.hpp>
#include "y2k_dsp/recording/audio_file_importer.h"

using namespace y2k::dsp;

TEST_CASE("AudioFileImporter: supported extensions", "[importer]") {
    AudioFileImporter imp;
    REQUIRE(imp.canImport(juce::File("/path/to/file.wav")));
    REQUIRE(imp.canImport(juce::File("/path/to/file.aif")));
    REQUIRE(imp.canImport(juce::File("/path/to/file.flac")));
    REQUIRE(!imp.canImport(juce::File("/path/to/file.pdf")));
    REQUIRE(!imp.canImport(juce::File("/path/to/file.txt")));
}

TEST_CASE("AudioFileImporter: nonexistent file returns error", "[importer]") {
    AudioFileImporter imp;
    auto result = imp.import(juce::File("/does/not/exist.wav"), {}, 0.0, 120.0);
    REQUIRE(!result.ok);
    REQUIRE(result.error.isNotEmpty());
    REQUIRE(result.clip == nullptr);
}

TEST_CASE("AudioFileImporter: unsupported format returns error", "[importer]") {
    AudioFileImporter imp;
    // Create a temp file with wrong extension
    const juce::File f = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile("test.pdf");
    f.create();
    auto result = imp.import(f, {}, 0.0, 120.0);
    REQUIRE(!result.ok);
    f.deleteFile();
}

TEST_CASE("AudioFileImporter: valid WAV import succeeds", "[importer]") {
    // Create a minimal WAV file for testing
    const juce::File tmpDir = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile("y2k_import_test");
    tmpDir.createDirectory();
    const juce::File wav = tmpDir.getChildFile("test.wav");

    // Write a tiny WAV via JUCE
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::FileOutputStream> fos(wav.createOutputStream());
    if (fos) {
        auto* writer = fmt.createWriterFor(fos.release(), 44100, 2, 16, {}, 0);
        if (writer) {
            juce::AudioBuffer<float> buf(2, 4410); buf.clear();
            writer->writeFromAudioSampleBuffer(buf, 0, 4410);
            writer->flush();
            delete writer;
        }
    }

    if (wav.existsAsFile()) {
        AudioFileImporter imp;
        auto result = imp.import(wav, {}, 0.0, 120.0);
        REQUIRE(result.ok);
        REQUIRE(result.clip != nullptr);
        REQUIRE(result.clip->sourceFile == wav);
        REQUIRE(result.durationSec > 0.0);
        REQUIRE(result.clip->durationBeats > 0.0);
    }
    tmpDir.deleteRecursively();
}
