// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The program as a process: what it prints, the exit code for each kind of failure, and that a
// failure leaves no output. The exit codes are the interface of a program that runs it (0 done,
// 1 the build failed, after the input was read or for want of memory or scratch space while it
// was read, 2 the input is invalid, 64 wrong command line).

#include <cstdlib>
#include <limits>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "cli_support.h"
#include "env_var_guard.h"
#include "model_files.h"
#include "container_file.h"
#include "temp_dir.h"
#include "version_file.h"

namespace {

using namespace bm3dtest;

// The input and the output are in folders of their own, so that whatever a run leaves in the
// folder of the output is seen
struct Folders {
    TempDir dir;
    fs::path in = dir.makeDir("in");
    fs::path out = dir.makeDir("out");

    std::string input(const std::string& name = "model.obj") const { return pathToUtf8(in / name); }
    std::string output(const std::string& name = "model.bm3d") const { return pathToUtf8(out / name); }

    void writeTriangle() const { writeTextFile(in / "model.obj", kTriangleObj); }
};

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> result;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);)
        result.push_back(line);
    return result;
}

void expectUsageError(const ProcessResult& r, const std::string& context) {
    EXPECT_EQ(r.exitCode, 64) << context << "\n" << r.output();
    EXPECT_NE(r.err.find("Usage: bm3d"), std::string::npos) << context << "\n" << r.err;
    // a refused command line is refused before the build, so no heartbeat is printed
    EXPECT_TRUE(r.out.empty()) << context << "\n" << r.out;
}

// The runner the tests rely on
TEST(ProcessRunner, aProgramThatIsNotThereIsReportedAsNotStarted) {
    TempDir dir;
    const ProcessResult r = runProcess(dir.file("absent"), {});
    EXPECT_EQ(r.exitCode, -1);
    EXPECT_TRUE(r.out.empty());
}

TEST(ProcessRunner, theTwoOutputStreamsAreCapturedApart) {
    const ProcessResult usage = runBm3d({"--bogus"});
    EXPECT_TRUE(usage.out.empty()) << usage.out;
    EXPECT_NE(usage.err.find("unknown option --bogus"), std::string::npos) << usage.err;
    const ProcessResult help = runBm3d({"--help"});
    EXPECT_TRUE(help.err.empty()) << help.err;
    EXPECT_FALSE(help.out.empty());
}

TEST(CliInfo, theVersionIsPrintedAsTheNameTheVersionAndTheFormat) {
    const std::string version = versionFileText();
    ASSERT_FALSE(version.empty());
    const ProcessResult r = runBm3d({"--version"});
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_EQ(r.out, "bm3d " + version + " (format 0)\n");
    EXPECT_TRUE(r.err.empty()) << r.err;
}

TEST(CliInfo, theHelpIsPrintedToStdoutWithExitCodeZero) {
    for (const char* flag : {"--help", "-h"}) {
        const ProcessResult r = runBm3d({flag});
        EXPECT_EQ(r.exitCode, 0) << flag;
        EXPECT_EQ(r.out.rfind("Usage: bm3d", 0), 0u) << flag << "\n" << r.out;
        EXPECT_NE(r.out.find("--threads"), std::string::npos) << flag;
        EXPECT_NE(r.out.find("--metadata"), std::string::npos) << flag;
        EXPECT_NE(r.out.find("--version"), std::string::npos) << flag;
        EXPECT_NE(r.out.find("Exit codes"), std::string::npos) << flag;
        EXPECT_TRUE(r.err.empty()) << flag << "\n" << r.err;
    }
}

TEST(CliInfo, theIntervalOfTheHeartbeatForTestsIsNotInTheHelp) {
    EXPECT_EQ(runBm3d({"--help"}).out.find("HEARTBEAT"), std::string::npos);
}

TEST(CliArguments, wrongArgumentsAreAUsageErrorAndBuildNothing) {
    Folders f;
    f.writeTriangle();
    writeTextFile(f.in / "metadata.json", "{}");
    const std::string in = f.input(), out = f.output(), json = pathToUtf8(f.in / "metadata.json");
    const std::vector<std::vector<std::string>> wrong = {
        {},
        {in},
        {in, out, "extra"},
        {"--bogus", in, out},
        {"-x", in, out},
        {in, out, "--bogus"},
        {"--threads"},
        {"--threads", "2"},
        {"--threads", "1", "--threads", "2", in, out},
        {"--metadata"},
        {"--metadata", json},
        {"--metadata", json, "--metadata", json, in, out},
        {"--version", "extra"},
        {"--version", in, out},
        {"--help", "extra"},
    };
    for (const auto& arguments : wrong) {
        std::string context;
        for (const auto& a : arguments)
            context += "'" + a + "' ";
        expectUsageError(runBm3d(arguments), context);
    }
    EXPECT_TRUE(f.dir.list("out").empty());
}

TEST(CliArguments, aThreadCountThatIsNotAWholeNumberFromOneTo4096IsAUsageError) {
    Folders f;
    f.writeTriangle();
    // 0 and a negative count, text, a count past 4096, an empty value, and the forms a lenient
    // parser would take: a fraction, a sign, a space, an exponent, a hexadecimal number
    for (const char* bad : {"0", "-1", "two", "99999", "", "4097", "1.5", "+1", " 1", "1 ", "1e2", "0x10", "1,000"}) {
        const ProcessResult r = runBm3d({"--threads", bad, f.input(), f.output()});
        expectUsageError(r, std::string("--threads '") + bad + "'");
        EXPECT_NE(r.err.find("--threads takes a whole number from 1 to 4096"), std::string::npos) << bad << "\n" << r.err;
        EXPECT_FALSE(fs::exists(f.out / "model.bm3d")) << bad;
    }
    EXPECT_TRUE(f.dir.list("out").empty());
}

TEST(CliArguments, theToolTakesAThreadCountOfOneOrMore) {
    Folders f;
    f.writeTriangle();
    for (const char* threads : {"1", "2", "0004"}) {
        const std::string out = f.output(std::string("threads") + threads + ".bm3d");
        const ProcessResult r = runBm3d({"--threads", threads, f.input(), out});
        EXPECT_EQ(r.exitCode, 0) << threads << "\n" << r.output();
        const ContainerFile h = readContainerFile(pathFromUtf8(out));
        EXPECT_EQ(h.magic, kContainerMagic) << threads;
        EXPECT_EQ(h.nface, 1u) << threads;
    }
}

TEST(CliArguments, theOptionsMayComeAfterThePaths) {
    Folders f;
    f.writeTriangle();
    const ProcessResult r = runBm3d({f.input(), f.output(), "--threads", "1"});
    EXPECT_EQ(r.exitCode, 0) << r.output();
    EXPECT_TRUE(fs::exists(f.out / "model.bm3d"));
}

TEST(CliArguments, twoDashesEndTheOptionsSoThatAPathMayStartWithADash) {
    Folders f;
    writeTextFile(f.in / "-model.obj", kTriangleObj);
    EXPECT_EQ(runBm3d({f.input("-model.obj"), f.output("-out.bm3d")}).exitCode, 0);
    const ProcessResult r = runBm3d({"--", f.input("-model.obj"), f.output("-out.bm3d")});
    EXPECT_EQ(r.exitCode, 0) << r.output();
    EXPECT_TRUE(fs::exists(f.out / "-out.bm3d"));
}

TEST(CliExitCodes, aBuildThatWorksExitsWithZeroAndWritesTheOutput) {
    Folders f;
    f.writeTriangle();
    const ProcessResult r = runBm3d({f.input(), f.output()});
    EXPECT_EQ(r.exitCode, 0) << r.output();
    EXPECT_TRUE(r.err.empty()) << r.err;
    EXPECT_EQ(f.dir.list("out"), std::vector<std::string>{"model.bm3d"});
    const ContainerFile h = readContainerFile(f.out / "model.bm3d");
    EXPECT_EQ(h.magic, kContainerMagic);
    EXPECT_EQ(h.nface, 1u);
}

TEST(CliExitCodes, aMissingInputExitsWithTwoAndNamesTheFile) {
    Folders f;
    const ProcessResult r = runBm3d({f.input("absent.obj"), f.output()});
    EXPECT_EQ(r.exitCode, 2) << r.output();
    EXPECT_NE(r.err.find("invalid input"), std::string::npos) << r.err;
    EXPECT_NE(r.err.find("absent.obj"), std::string::npos) << r.err;
    EXPECT_TRUE(f.dir.list("out").empty());
}

TEST(CliExitCodes, aTextFileNamedObjExitsWithTwoAndWritesNoOutput) {
    Folders f;
    writeTextFile(f.in / "x.obj", "this is not a model\n");
    const ProcessResult r = runBm3d({f.input("x.obj"), f.output()});
    EXPECT_EQ(r.exitCode, 2) << r.output();
    EXPECT_NE(r.err.find("invalid input"), std::string::npos) << r.err;
    EXPECT_TRUE(f.dir.list("out").empty());
}

TEST(CliExitCodes, aFileOfAFormatTheProgramDoesNotReadExitsWithTwoAndNamesTheFormatsItReads) {
    Folders f;
    writeTextFile(f.in / "model.off", "OFF\n3 1 0\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n");
    const ProcessResult r = runBm3d({f.input("model.off"), f.output()});
    EXPECT_EQ(r.exitCode, 2) << r.output();
    EXPECT_NE(r.err.find("invalid input"), std::string::npos) << r.err;
    EXPECT_NE(r.err.find("model.off"), std::string::npos) << r.err;
    for (const char* format : {"obj", "ply", "stl"})
        EXPECT_NE(r.err.find(format), std::string::npos) << r.err;
    EXPECT_TRUE(f.dir.list("out").empty());
}

// A file that opens with the TIFF magic number is a TIFF texture. With libtiff it does not parse; a
// program built without it says so, and both are invalid input.
TEST(CliExitCodes, aTiffTextureThatCannotBeReadExitsWithTwoAndNamesIt) {
    Folders f;
    writeTexturedModel(f.in, "texture.tif");
    writeBinaryFile(f.in / "texture.tif", std::string("II*\0\x08\0\0\0", 8));
    const ProcessResult r = runBm3d({f.input(), f.output()});
    EXPECT_EQ(r.exitCode, 2) << r.output();
    EXPECT_NE(r.err.find("texture.tif"), std::string::npos) << r.err;
#ifndef BM3D_WITH_TIFF
    EXPECT_NE(r.err.find("without TIFF support"), std::string::npos) << r.err;
#endif
    EXPECT_TRUE(f.dir.list("out").empty());
}

TEST(CliExitCodes, aModelWithAnUpperCaseExtensionIsBuilt) {
    Folders f;
    writeTextFile(f.in / "MODEL.OBJ", kTriangleObj);
    const ProcessResult r = runBm3d({f.input("MODEL.OBJ"), f.output()});
    EXPECT_EQ(r.exitCode, 0) << r.output();
    EXPECT_EQ(readContainerFile(f.out / "model.bm3d").nface, 1u);
}

TEST(CliExitCodes, aBrokenPlyExitsWithTwoAndWritesNoOutput) {
    Folders f;
    writeTextFile(f.in / "bad.ply", "ply\nformat ascii 1.0\nelement vertex 3\n");
    const ProcessResult r = runBm3d({f.input("bad.ply"), f.output()});
    EXPECT_EQ(r.exitCode, 2) << r.output();
    EXPECT_TRUE(f.dir.list("out").empty());
}

// A NaN in a binary STL used to make the build run for ever; the time limit makes that a failure
// of the test, not of the whole run
TEST(CliExitCodes, aCoordinateThatIsNotANumberExitsWithTwoAndNamesTheFile) {
    Folders f;
    auto triangles = waveTriangles(2);
    triangles[3][1][0] = std::numeric_limits<float>::quiet_NaN();
    writeBinaryFile(f.in / "nan.stl", binaryStl(triangles));
    const ProcessResult r = runProcess(bm3dExecutable(), {f.input("nan.stl"), f.output()}, std::chrono::seconds(60));
    EXPECT_FALSE(r.timedOut);
    EXPECT_EQ(r.exitCode, 2) << r.output();
    EXPECT_NE(r.err.find("invalid input"), std::string::npos) << r.err;
    EXPECT_NE(r.err.find("nan.stl"), std::string::npos) << r.err;
    EXPECT_NE(r.err.find("not a finite number"), std::string::npos) << r.err;
    EXPECT_TRUE(f.dir.list("out").empty());
}

// A file of random bytes named .stl used to keep the program busy for ever: its numbers are floats
// of any size, the bounding sphere of the model never ended. It is refused at once. The time limit
// makes a program that does not end a failure of the test, not of the whole run.
TEST(CliExitCodes, aFileOfRandomBytesNamedStlExitsWithTwoAtOnce) {
    for (uint32_t seed = 1; seed <= 12; seed++) {
        Folders f;
        std::string bytes;
        uint32_t state = seed * 2654435761u;
        for (int i = 0; i < 3000; i++) {
            state = state * 1664525u + 1013904223u;  // a linear congruential generator, the same on every platform
            bytes.push_back(char(state >> 24));
        }
        writeBinaryFile(f.in / "random.stl", bytes);
        const ProcessResult r = runProcess(bm3dExecutable(), {f.input("random.stl"), f.output()}, std::chrono::seconds(30));
        EXPECT_FALSE(r.timedOut) << "seed " << seed;
        EXPECT_EQ(r.exitCode, 2) << "seed " << seed << "\n" << r.output();
        EXPECT_NE(r.err.find("random.stl"), std::string::npos) << r.err;
        EXPECT_TRUE(f.dir.list("out").empty());
    }
}

// The same hang, from a file that is a binary STL: its coordinates are finite but too large to measure
TEST(CliExitCodes, aBinaryStlOfHugeCoordinatesExitsWithTwoAtOnce) {
    Folders f;
    const Triangle triangle{{{1e38f, -1e38f, 3e37f}, {-2e38f, 1e38f, 0}, {1e37f, 3e38f, -3e38f}}};
    writeBinaryFile(f.in / "huge.stl", binaryStl({triangle}));
    const ProcessResult r = runProcess(bm3dExecutable(), {f.input("huge.stl"), f.output()}, std::chrono::seconds(30));
    EXPECT_FALSE(r.timedOut);
    EXPECT_EQ(r.exitCode, 2) << r.output();
    EXPECT_NE(r.err.find("units across"), std::string::npos) << r.err;
    EXPECT_TRUE(f.dir.list("out").empty());
}

// The model is read, and its triangles cannot be divided into nodes: the failure is of the build
TEST(CliExitCodes, aBuildThatFailsAfterTheInputIsReadExitsWithOneAndWritesNoOutput) {
    Folders f;
    std::string obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\n";
    for (int i = 0; i < 100000; i++)
        obj += "f 1 2 3\n";
    writeTextFile(f.in / "same.obj", obj);
    const ProcessResult r = runBm3d({f.input("same.obj"), f.output()});
    EXPECT_EQ(r.exitCode, 1) << r.output();
    EXPECT_NE(r.err.find("could not build"), std::string::npos) << r.err;
    EXPECT_NE(r.err.find("too small to divide"), std::string::npos) << r.err;
    EXPECT_TRUE(f.dir.list("out").empty());
}

// The whole model is built and fails at the last step, when the finished file is given the name
// of the output, which is the name of a folder
TEST(CliExitCodes, anOutputThatIsAFolderExitsWithOneAfterTheWholeBuild) {
    Folders f;
    f.writeTriangle();
    f.dir.makeDir("out/model.bm3d");
    const ProcessResult r = runBm3d({f.input(), f.output()});
    EXPECT_EQ(r.exitCode, 1) << r.output();
    EXPECT_NE(r.err.find("could not build"), std::string::npos) << r.err;
    EXPECT_EQ(f.dir.list("out"), std::vector<std::string>{"model.bm3d"});
    EXPECT_TRUE(f.dir.list("out/model.bm3d").empty());
}

TEST(CliExitCodes, aFailedRunLeavesTheEarlierOutputAsItWas) {
    Folders f;
    writeTextFile(f.in / "x.obj", "this is not a model\n");
    writeTextFile(f.out / "model.bm3d", "an earlier build");
    EXPECT_EQ(runBm3d({f.input("x.obj"), f.output()}).exitCode, 2);
    EXPECT_EQ(f.dir.list("out"), std::vector<std::string>{"model.bm3d"});
    EXPECT_EQ(fs::file_size(f.out / "model.bm3d"), 16u);
}

// The input is fine and the scratch file that reading it needs cannot be created: the OBJ loader
// keeps its vertex cache in the temporary folder, which is a file
TEST(CliExitCodes, aTemporaryFolderThatCannotTakeAScratchFileExitsWithOne) {
    Folders f;
    f.writeTriangle();
    const fs::path notAFolder = f.dir.file("not-a-folder");
    writeTextFile(notAFolder, "");
    const TemporaryFolderGuard temporary(pathToUtf8(notAFolder));
    const ProcessResult r = runBm3d({f.input(), f.output()});
    EXPECT_EQ(r.exitCode, 1) << r.output();
    EXPECT_NE(r.err.find("could not build"), std::string::npos) << r.err;
    EXPECT_TRUE(f.dir.list("out").empty());
}

#ifndef _WIN32
// A scratch file that cannot grow while the input is read: the program runs under a limit on the
// size of the files it writes (with the signal of that limit ignored, so that the write fails
// instead of killing it). The OBJ loader's vertex cache is in the temporary folder; the triangles
// of an STL go straight to the stream's scratch file next to the output.
TEST(CliExitCodes, aScratchFileThatCannotGrowWhileTheInputIsReadExitsWithOne) {
    Folders f;
    f.writeTriangle();
    writeTextFile(f.in / "model.stl", asciiStl(waveTriangles(2)));
    for (const char* input : {"model.obj", "model.stl"}) {
        const ProcessResult r = runProcess("/bin/sh", {"-c", "trap '' XFSZ; ulimit -f 16 && exec \"$0\" \"$@\"",
                                                       pathToUtf8(bm3dExecutable()), f.input(input), f.output()});
        EXPECT_EQ(r.exitCode, 1) << input << "\n" << r.output();
        EXPECT_NE(r.err.find("could not build"), std::string::npos) << input << "\n" << r.err;
        EXPECT_TRUE(f.dir.list("out").empty()) << input;
    }
}
#endif

// The textures the materials name are part of the input
TEST(CliExitCodes, aMissingTextureExitsWithTwoAndNamesIt) {
    Folders f;
    writeTexturedModel(f.in, "texture.png");
    const ProcessResult r = runBm3d({f.input(), f.output()});
    EXPECT_EQ(r.exitCode, 2) << r.output();
    EXPECT_NE(r.err.find("invalid input"), std::string::npos) << r.err;
    EXPECT_NE(r.err.find("texture.png"), std::string::npos) << r.err;
    EXPECT_TRUE(f.dir.list("out").empty());
}

TEST(CliExitCodes, aTextureThatCannotBeDecodedExitsWithTwo) {
    Folders f;
    writeTexturedModel(f.in, "texture.png");
    writeTextFile(f.in / "texture.png", "this is not a picture");
    const ProcessResult r = runBm3d({f.input(), f.output()});
    EXPECT_EQ(r.exitCode, 2) << r.output();
    EXPECT_NE(r.err.find("invalid input"), std::string::npos) << r.err;
    EXPECT_TRUE(f.dir.list("out").empty());
}

TEST(CliMetadata, aJsonObjectIsAccepted) {
    Folders f;
    f.writeTriangle();
    writeTextFile(f.in / "metadata.json", "{\"title\":\"Модель\",\"tags\":[1,2,{\"k\":null}]}\n");
    const ProcessResult r = runBm3d({"--metadata", pathToUtf8(f.in / "metadata.json"), f.input(), f.output()});
    EXPECT_EQ(r.exitCode, 0) << r.output();
    EXPECT_EQ(readContainerFile(f.out / "model.bm3d").nface, 1u);
}

TEST(CliMetadata, anObjectWithAByteOrderMarkIsAccepted) {
    Folders f;
    f.writeTriangle();
    writeTextFile(f.in / "metadata.json", "\xEF\xBB\xBF{\"title\":\"x\"}");
    EXPECT_EQ(runBm3d({"--metadata", pathToUtf8(f.in / "metadata.json"), f.input(), f.output()}).exitCode, 0);
}

TEST(CliMetadata, whatIsNotAJsonObjectIsAUsageErrorThatNamesTheFile) {
    Folders f;
    f.writeTriangle();
    const std::vector<std::pair<const char*, std::string>> bad = {
        {"array.json", "[1,2,3]"},
        {"trailing.json", "{\"a\":1,}"},
        {"text.json", "not json"},
        {"empty.json", ""},
        {"utf8.json", "{\"a\":\"\xFF\"}"},
        {"surrogate.json", "{\"a\":\"\\ud800\"}"},
        {"after.json", "{} {}"},
    };
    for (const auto& [name, text] : bad) {
        writeTextFile(f.in / name, text);
        const ProcessResult r = runBm3d({"--metadata", pathToUtf8(f.in / name), f.input(), f.output()});
        expectUsageError(r, name);
        EXPECT_NE(r.err.find(name), std::string::npos) << name << "\n" << r.err;
        EXPECT_NE(r.err.find("not a JSON object"), std::string::npos) << name << "\n" << r.err;
    }
    EXPECT_TRUE(f.dir.list("out").empty());
}

TEST(CliMetadata, aMetadataFileThatCannotBeReadIsAUsageError) {
    Folders f;
    f.writeTriangle();
    const ProcessResult r = runBm3d({"--metadata", pathToUtf8(f.in / "absent.json"), f.input(), f.output()});
    expectUsageError(r, "absent.json");
    EXPECT_NE(r.err.find("absent.json"), std::string::npos) << r.err;
    EXPECT_TRUE(f.dir.list("out").empty());
}

// The command line is checked as a whole before the model is touched
TEST(CliMetadata, aBadMetadataFileIsReportedBeforeAMissingInput) {
    Folders f;
    writeTextFile(f.in / "metadata.json", "[]");
    const ProcessResult r = runBm3d({"--metadata", pathToUtf8(f.in / "metadata.json"), f.input("absent.obj"), f.output()});
    EXPECT_EQ(r.exitCode, 64) << r.output();
}

// While a build runs the program prints one line at once and one per interval, to stdout, so
// that a supervisor that watches the output sees a long build is alive
class CliHeartbeat : public ::testing::Test {
protected:
    // A model that takes the build a while: tens of thousands of triangles
    void writeLargeModel(const Folders& f) {
        writeBinaryFile(f.in / "large.stl", binaryStl(waveTriangles(250)));
    }

    // The elapsed seconds in "bm3d: building <input> (<n> s)"
    static std::vector<long> elapsedOf(const std::vector<std::string>& heartbeat, const std::string& input) {
        std::vector<long> seconds;
        const std::string prefix = "bm3d: building " + input + " (";
        for (const std::string& line : heartbeat) {
            EXPECT_EQ(line.rfind(prefix, 0), 0u) << line;
            EXPECT_TRUE(std::regex_match(line.substr(prefix.size()), std::regex(R"(\d+ s\))"))) << line;
            seconds.push_back(std::atol(line.c_str() + prefix.size()));
        }
        return seconds;
    }
};

TEST_F(CliHeartbeat, aShortBuildPrintsTheLineOfItsStartOnce) {
    Folders f;
    f.writeTriangle();
    const ProcessResult r = runBm3d({f.input(), f.output()});
    ASSERT_EQ(r.exitCode, 0) << r.output();
    const auto heartbeat = lines(r.out);
    ASSERT_EQ(heartbeat.size(), 1u) << r.out;
    EXPECT_EQ(heartbeat[0], "bm3d: building " + f.input() + " (0 s)");
}

TEST_F(CliHeartbeat, aLongBuildPrintsALinePerInterval) {
    Folders f;
    writeLargeModel(f);
    EnvVarGuard interval("BM3D_TEST_HEARTBEAT_MS", "20");
    const ProcessResult r = runBm3d({"--threads", "1", f.input("large.stl"), f.output()});
    ASSERT_EQ(r.exitCode, 0) << r.output();
    const auto heartbeat = lines(r.out);
    EXPECT_GE(heartbeat.size(), 5u) << "the build took less than 100 ms\n" << r.out;
    const auto seconds = elapsedOf(heartbeat, f.input("large.stl"));
    for (size_t i = 1; i < seconds.size(); ++i)
        EXPECT_GE(seconds[i], seconds[i - 1]);
    // the heartbeat is for the supervisor on stdout; stderr keeps the messages
    EXPECT_EQ(r.err.find("building"), std::string::npos) << r.err;
}

TEST_F(CliHeartbeat, aValueThatIsNotAPositiveNumberOfMillisecondsKeepsTheDefaultInterval) {
    // The line of the start, then one per 15 s. The count of lines depends on how long the build
    // takes (more than 15 s under the sanitizers), the time each line shows does not.
    const long defaultIntervalSeconds = 15;
    Folders f;
    writeLargeModel(f);
    for (const char* value : {"0", "abc", "-5", ""}) {
        EnvVarGuard interval("BM3D_TEST_HEARTBEAT_MS", value);
        const ProcessResult r = runBm3d({"--threads", "1", f.input("large.stl"), f.output()});
        ASSERT_EQ(r.exitCode, 0) << value << "\n" << r.output();
        const auto seconds = elapsedOf(lines(r.out), f.input("large.stl"));
        ASSERT_FALSE(seconds.empty()) << "'" << value << "'\n" << r.out;
        for (size_t i = 0; i < seconds.size(); ++i)
            EXPECT_GE(seconds[i], defaultIntervalSeconds * long(i)) << "'" << value << "'\n" << r.out;
    }
}

TEST_F(CliHeartbeat, aBuildThatFailsStopsTheHeartbeatAndReportsOnStderr) {
    Folders f;
    EnvVarGuard interval("BM3D_TEST_HEARTBEAT_MS", "20");
    const ProcessResult r = runBm3d({f.input("absent.obj"), f.output()});
    EXPECT_EQ(r.exitCode, 2);
    EXPECT_FALSE(r.timedOut);
    EXPECT_NE(r.err.find("invalid input"), std::string::npos) << r.err;
}

}  // namespace
