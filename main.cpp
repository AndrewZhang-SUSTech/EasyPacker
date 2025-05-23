#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <string>
#include <optional>
#include <regex>
#include <cmath>
#include <filesystem>
#include <windows.h>
#include <cxxopts.hpp>
#include <picosha2.h>
#include "unpacker_template.hpp"

namespace fs = std::filesystem;

// Constants namespace
namespace constants {
    constexpr int kMB = 1024 * 1024;
    constexpr char kSevenZipExecutable[] = "7zr.exe";
    constexpr char kSevenZipDownloadUrl[] = "https://7-zip.org/a/7zr.exe ";
    constexpr char kDefaultOutputDir[] = ".\\output";
    constexpr char kDefaultArchiveName[] = "archive";
    constexpr char kDefaultVolumeSizeGB[] = "10.0";
    constexpr char kVersion[] = "1.0.2";
}

// Function declarations
std::optional<cxxopts::ParseResult> ParseCommandLineArgs(int argc, char** argv);
bool Setup7ZipExecutable();
bool CleanupOldArchiveFiles(const std::string& output_dir, const std::string& archive_name);
std::string BuildCompressionCommand(const std::string& output_dir,
                                    const std::string& archive_name,
                                    const std::vector<std::string>& input_files,
                                    int volume_size_kb);
std::optional<std::vector<fs::path>> RenameVolumeFiles(const std::string& output_dir,
                                                      const std::string& archive_name);
std::optional<std::vector<std::string>> CalculateFileHashes(const std::vector<fs::path>& files);
void GenerateUnpackerScript(const std::string& output_dir,
                           const std::vector<fs::path>& files,
                           const std::vector<std::string>& hashes);

int main(int argc, char** argv) {
    // Set working directory to executable location
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    fs::current_path(fs::path(exe_path).parent_path());

    auto parse_result = ParseCommandLineArgs(argc, argv);
    if (!parse_result) {
        return 0;
    }

    std::vector<std::string> input_files = parse_result.value()["file"].as<std::vector<std::string>>();
    std::string output_dir = parse_result.value()["output"].as<std::string>();
    std::string output_name = parse_result.value()["name"].as<std::string>();
    int volume_size = round(parse_result.value()["size"].as<double>() * 1024 * 1024); // Convert GB to KB
    std::cout << "Input files: ";
    for (const auto& file : input_files) {
        std::cout << file << " ";
    }
    std::cout << "\nOutput directory: " << output_dir
              << "\nOutput name: " << output_name
              << "\nVolume size: " << (volume_size / constants::kMB) << " MB\n";

    if (!Setup7ZipExecutable()) {
        system("pause");
        return 1;
    }

    if (!CleanupOldArchiveFiles(output_dir, output_name)) {
        system("pause");
        return 1;
    }

    const std::string compress_cmd = BuildCompressionCommand(
        output_dir, output_name, input_files, volume_size
    );

    if (system(compress_cmd.c_str()) != 0) {
        std::cerr << "Error: Failed to execute command. Please check the command and try again.";
        system("pause");
        return 1;
    }

    auto volume_files = RenameVolumeFiles(output_dir, output_name);
    if (!volume_files) {
        system("pause");
        return 1;
    }

    std::sort(volume_files->begin(), volume_files->end());

    auto file_hashes = CalculateFileHashes(*volume_files);
    if (!file_hashes) {
        system("pause");
        return 1;
    }

    try {
        GenerateUnpackerScript(output_dir, *volume_files, *file_hashes);
        std::cout << "\nUnpacker script generated: "
                  << output_dir << "\\unpacker.bat" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to generate unpacker script: " << e.what() << std::endl;
        system("pause");
        return 1;
    }

    system("pause");
    return 0;
}

std::optional<cxxopts::ParseResult> ParseCommandLineArgs(int argc, char** argv) {
    cxxopts::Options options("EasyPacker", "A simple packing tool");
    options.add_options()
        ("h,help", "Show help")
        ("v,version", "Show version")
        ("o,output", "Path to output directory",
         cxxopts::value<std::string>()->default_value(constants::kDefaultOutputDir))
        ("n,name", "Name of the output file",
         cxxopts::value<std::string>()->default_value(constants::kDefaultArchiveName))
        ("f,file", "Path to input files",
         cxxopts::value<std::vector<std::string>>())
        ("s,size", "Size of each volume (GB)",
         cxxopts::value<double>()->default_value(constants::kDefaultVolumeSizeGB));

    options.parse_positional({"file"});

    try {
        auto result = options.parse(argc, argv);

        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            return std::nullopt;
        }

        if (result.count("version")) {
            std::cout << "EasyPacker version " << constants::kVersion << std::endl;
            return std::nullopt;
        }

        if (!result.count("file")) {
            std::cerr << "Error: No input files specified." << std::endl;
            std::cout << "Use -h for more information." << std::endl;
            return std::nullopt;
        }

        return result;
    } catch (const cxxopts::exceptions::parsing& e) {
        std::cerr << "Error: Failed to parse arguments: " << e.what() << std::endl;
        return std::nullopt;
    }
}

bool Setup7ZipExecutable() {
    if (fs::exists(constants::kSevenZipExecutable)) {
        return true;
    }

    std::cout << "Error: 7zr.exe not found in current directory. Downloading...";
    std::string download_cmd = "curl -L -o ";
    download_cmd += constants::kSevenZipExecutable;
    download_cmd += " ";
    download_cmd += constants::kSevenZipDownloadUrl;

    if (system(download_cmd.c_str()) != 0) {
        std::cerr << "Error: Failed to download 7zr.exe. You can download it manually from "
                  << constants::kSevenZipDownloadUrl << std::endl;
        return false;
    }

    std::cout << "Download complete.\n" << std::endl;
    return true;
}

bool CleanupOldArchiveFiles(const std::string& output_dir, const std::string& archive_name) {
    try {
        if (!fs::exists(output_dir)) {
            fs::create_directories(output_dir);
            return true;
        }

        const std::string pattern = "^" + archive_name + R"((\.(7z\.)?(\d{3}|bat)$)";
        const std::regex file_pattern(pattern);

        for (const auto& entry : fs::directory_iterator(output_dir)) {
            if (!entry.is_regular_file()) continue;
            const std::string filename = entry.path().filename().string();
            if (std::regex_match(filename, file_pattern)) {
                fs::remove(entry.path());
                std::cout << "Deleted: " << entry.path() << std::endl;
            }
        }

        return true;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error: " << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return false;
    }
}

std::string BuildCompressionCommand(const std::string& output_dir,
                                    const std::string& archive_name,
                                    const std::vector<std::string>& input_files,
                                    int volume_size_kb) {
    std::ostringstream cmd;
    cmd << ".\\" << constants::kSevenZipExecutable << " a -t7z ";

    // Output path with quotes to handle spaces
    cmd << "\"" << output_dir << "\\" << archive_name << "\" ";

    // Input files list
    for (const auto& file : input_files) {
        cmd << "\"" << file << "\" ";
    }

    // Compression parameters
    cmd << "-mx=9 "          // Maximum compression level
        << "-ms=200m "       // Solid block size
        << "-mf "            // Enable BCJ filters
        << "-mhc "           // Enable header compression
        << "-mhcf "          // Enable full header compression
        << "-m0=LZMA "       // Use LZMA algorithm
        << "-mmt "           // Multi-threading
        << "-r ";            // Recurse subdirectories

    // Volume size
    cmd << "-v" << volume_size_kb << "k";

    return cmd.str();
}

std::optional<std::vector<fs::path>> RenameVolumeFiles(const std::string& output_dir,
                                                      const std::string& archive_name) {
    try {
        const fs::path dir_path(output_dir);
        const std::regex pattern("^" + archive_name + R"(\.7z\.(\d{3})$)");
        std::vector<fs::path> renamed_files;

        for (const auto& entry : fs::directory_iterator(dir_path)) {
            if (!entry.is_regular_file()) continue;
            const std::string filename = entry.path().filename().string();
            std::smatch matches;

            if (std::regex_match(filename, matches, pattern)) {
                const fs::path old_path = entry.path();
                const fs::path new_path = dir_path / (archive_name + "." + matches[1].str());

                fs::rename(old_path, new_path);
                std::cout << "Renamed: " << old_path.filename().string()
                         << " -> " << new_path.filename().string() << std::endl;

                renamed_files.push_back(new_path);
            }
        }

        return renamed_files;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Failed to rename files: " << e.what() << std::endl;
        return std::nullopt;
    }
}

std::optional<std::vector<std::string>> CalculateFileHashes(const std::vector<fs::path>& files) {
    std::vector<std::string> hashes;
    hashes.reserve(files.size());

    for (const auto& file_path : files) {
        std::ifstream file_stream(file_path, std::ios::binary);
        if (!file_stream) {
            std::cerr << "Failed to open file: " << file_path << std::endl;
            return std::nullopt;
        }

        std::vector<unsigned char> hash(picosha2::k_digest_size);
        picosha2::hash256(file_stream, hash.begin(), hash.end());

        std::ostringstream hex_stream;
        hex_stream << std::hex << std::nouppercase << std::setfill('0');
        for (unsigned char byte : hash) {
            hex_stream << std::setw(2) << static_cast<int>(byte);
        }

        hashes.push_back(hex_stream.str());
    }

    return hashes;
}

void GenerateUnpackerScript(const std::string& output_dir,
                           const std::vector<fs::path>& files,
                           const std::vector<std::string>& hashes) {
    const fs::path script_path = fs::path(output_dir) / "unpacker.bat";

    // Copy 7zr.exe to output directory
    try {
        const fs::path source_7zr = constants::kSevenZipExecutable;
        const fs::path target_7zr = fs::path(output_dir) / constants::kSevenZipExecutable;

        if (fs::exists(source_7zr)) {
            fs::copy_file(source_7zr, target_7zr, fs::copy_options::overwrite_existing);
            std::cout << "[Copy] 7zr.exe copied to output directory" << std::endl;
        }
    } catch (const fs::filesystem_error& e) {
        throw std::runtime_error("Failed to copy 7zr.exe: " + std::string(e.what()));
    }

    // Generate batch script
    std::ofstream script_file(script_path);
    if (!script_file) {
        throw std::runtime_error("Failed to create unpacker script");
    }

    std::string script_content(kUnpackerTemplate);

    // Generate file array definition
    std::ostringstream files_definition;
    for (size_t i = 0; i < files.size(); ++i) {
        files_definition << "set files[" << i << "]="
                        << files[i].filename().string() << "^|"
                        << hashes[i] << "\n";
    }

    // Replace template placeholders
    const std::string files_placeholder = ":: GENERATED_FILES_ARRAY_PLACEHOLDER";
    const std::string count_placeholder = "VOLUME_COUNT_PLACEHOLDER";

    // Replace file array
    size_t pos = script_content.find(files_placeholder);
    if (pos != std::string::npos) {
        script_content.replace(pos, files_placeholder.length(), files_definition.str());
    }

    // Replace volume count
    pos = script_content.find(count_placeholder);
    if (pos != std::string::npos) {
        script_content.replace(pos, count_placeholder.length(),
                              std::to_string(files.size() - 1));
    }

    script_file << script_content;
    if (!script_file) {
        throw std::runtime_error("Failed to write unpacker script");
    }
}