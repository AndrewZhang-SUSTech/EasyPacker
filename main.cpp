#include <iostream>
#include <filesystem>
#include <cxxopts.hpp>
#include <picosha2.h>
#include <cmath>
#include <optional>
#include <regex>
#include <sstream>
#include <iomanip>
#include "unpacker_template.hpp"

std::optional<cxxopts::ParseResult> parse_args(int argc, char **argv);
bool configure_7zip();
bool delete_old_files(std::string_view output_dir, std::string_view output_name);
std::string build7zCommand(std::string_view output_dir, std::string_view output_name, const std::vector<std::string> &input_files, int volume_size);
std::optional<std::vector<std::filesystem::path>> rename_files(std::string_view output_dir, std::string_view output_name);
std::optional<std::vector<std::string>> calculate_hashes(const std::vector<std::filesystem::path>& files);
void generate_unpacker(const std::string& output_dir,
                      const std::vector<std::filesystem::path>& files,
                      const std::vector<std::string>& hashes);

int main(int argc, char **argv)
{
    // 获取可执行文件所在目录并切换工作目录
    std::filesystem::path exePath = std::filesystem::absolute(argv[0]);
    std::filesystem::current_path(exePath.parent_path());
    
    auto result = parse_args(argc, argv);
    if (!result) 
    {
        return 0;
    }

    std::vector<std::string> input_files = result.value()["file"].as<std::vector<std::string>>();
    std::string output_dir = result.value()["output"].as<std::string>();
    std::string output_name = result.value()["name"].as<std::string>();
    int volume_size = round(result.value()["size"].as<double>() * 1024 * 1024); // Convert GB to KB
    std::cout << "Input files: ";
    for (const auto &file : input_files)
    {
        std::cout << file << " ";
    }
    std::cout << "\nOutput directory: " << output_dir << "\nOutput name: " << output_name << "\nVolume size: " << volume_size << " KB\n" << std::endl;
    
    if (!configure_7zip()) 
    {
        system("pause");
        return 1;
    }

    if (!delete_old_files(output_dir, output_name))
    {
        system("pause");
        return 1;
    }
    

    std::string cmd = build7zCommand(output_dir, output_name, input_files, volume_size);
    // std::cout << "Executing command: " << cmd << std::endl;
    int ret = system(cmd.c_str());
    if (ret != 0)
    {
        std::cerr << "Error: Failed to execute command. Please check the command and try again.";
        system("pause");
        return 1;
    }
    printf("\n");

    auto renamed_files = rename_files(output_dir, output_name);
    if (!renamed_files)
    {
        system("pause");
        return 1;
    }

    std::sort(renamed_files.value().begin(), renamed_files.value().end());

    auto hashes = calculate_hashes(renamed_files.value());
    if (!hashes) {
        system("pause");
        return 1;
    }

    try {
        generate_unpacker(output_dir, renamed_files.value(), hashes.value());
        std::cout << "\nunpacker.bat generated: " << output_dir << "\\unpacker.bat" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to generate unpacker script: " << e.what() << std::endl;
        system("pause");
        return 1;
    }

    system("pause");
    return 0;
}

bool configure_7zip()
{
    std::filesystem::path exePath = "7zr.exe";
    if (!std::filesystem::exists(exePath))
    {
        std::cerr << "Error: 7zr.exe not found in current directory. Downloading...";
        int ret = system("curl -L -o 7zr.exe https://7-zip.com/a/7zr.exe");
        if(ret != 0)
        {
            std::cerr << "Error: Failed to download 7zr.exe. You can download it manually from https://7-zip.com/a/7zr.exe" << std::endl;
            return false;
        }
        std::cout << "Download complete.\n" << std::endl;
    }
    return true;
}

std::optional<cxxopts::ParseResult> parse_args(int argc, char **argv)
{
    cxxopts::Options options("EasyPacker", "A simple packing tool");
    options.add_options()
        ("h, help", "Show help")
        ("v, version", "Show version")
        ("o, output", "Path to output directory", cxxopts::value<std::string>()->default_value(".\\output"))
        ("n, name", "Name of the output file", cxxopts::value<std::string>()->default_value("archive"))
        ("f, file", "Path to input files", cxxopts::value<std::vector<std::string>>())
        ("s, size", "Size of each volume (GB)", cxxopts::value<double>()->default_value("10"));
    options.parse_positional({"file"});
    
    auto result = options.parse(argc, argv);
    if (result.count("help"))
    {
        std::cout << options.help() << std::endl;
        return std::nullopt;
    }
    if (result.count("version"))
    {
        std::cout << "EasyPacker version 1.0.0" << std::endl;
        return std::nullopt;
    }
    if (!result.count("file"))
    {
        std::cerr << "Error: No input files specified." << std::endl;
        std::cout << "Use -h for more information." << std::endl;
        return std::nullopt;
    }
    return result;
}

bool delete_old_files(std::string_view output_dir, std::string_view output_name)
{
    namespace fs = std::filesystem;
    try 
    {
        // 检查目录是否存在，不存在则创建
        if (!fs::exists(output_dir))
        {
            fs::create_directories(output_dir);
            return true;
        }

        std::string pattern_str = "^" + std::string(output_name) + "\\.(7z\\.)?\\d{3}$";
        std::regex file_pattern(pattern_str);

        for (const auto& entry : fs::directory_iterator(output_dir)) 
        {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (std::regex_match(filename, file_pattern)) {
                    std::filesystem::remove(entry.path());
                    std::cout << "Deleted: " << entry.path() << std::endl;
                }
            }
        }
    } catch (const fs::filesystem_error& e)
    {
        std::cerr << "Filesystem error: " << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) 
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return false;
    }
    return true;
}

std::string build7zCommand(std::string_view output_dir, std::string_view output_name, const std::vector<std::string> &input_files, int volume_size)
{
    std::string cmd;
    cmd.reserve(2048);  // 预分配足够空间，避免多次扩容

    // 起始部分
    cmd += ".\\7zr.exe a -t7z ";

    // 输出路径
    cmd += '"';
    cmd += output_dir;
    cmd += '\\';
    cmd += output_name;
    cmd += "\" ";

    // 输入文件（带引号防止空格路径出错）
    for (const auto& file : input_files) 
    {
        cmd += '"';
        cmd += file;
        cmd += "\" ";
    }

    // 参数配置
    cmd += "-mx=9 -ms=200m -mf -mhc -mhcf ";
    cmd += "-m0=LZMA -mmt -r ";

    // 分卷大小
    cmd += "-v";
    cmd += std::to_string(volume_size);
    cmd += 'k';
    return cmd;
}

std::optional<std::vector<std::filesystem::path>> rename_files(std::string_view output_dir, std::string_view output_name)
{
    namespace fs = std::filesystem;
    const fs::path dir_path{output_dir};
    const std::regex pattern{std::string("^") + std::string(output_name) + R"(\.7z\.(\d{3})$)"};
    
    std::vector<fs::path> renamed_paths;
    
    for (const auto& entry : fs::directory_iterator(dir_path)) 
    {
        if (!entry.is_regular_file()) 
        {
            continue;
        }

        const std::string filename = entry.path().filename().string();
        std::smatch matches;
        
        if (std::regex_match(filename, matches, pattern)) 
        {
            const fs::path old_path = entry.path();
            const fs::path new_path = dir_path / (std::string(output_name) + "." + matches[1].str());
            
            try 
            {
                fs::rename(old_path, new_path);
                std::cout << "Renamed: " << old_path.filename().string() 
                         << " -> " << new_path.filename().string() << '\n';
                renamed_paths.push_back(new_path);
            } 
            catch (const fs::filesystem_error& e) 
            {
                std::cerr << "Failed to rename " << old_path.filename().string() << ": " 
                         << e.what() << '\n';
                return std::nullopt;
            }
        }
    }

    return renamed_paths;
}

std::optional<std::vector<std::string>> calculate_hashes(const std::vector<std::filesystem::path>& files)
{
    std::vector<std::string> hashes;
    hashes.reserve(files.size());

    for (const auto& file : files)
    {
        std::ifstream fs(file, std::ios::binary);
        if (!fs)
        {
            std::cerr << "Failed to open file: " << file << std::endl;
            return std::nullopt;
        }

        std::vector<unsigned char> hash(picosha2::k_digest_size);
        picosha2::hash256(fs, hash.begin(), hash.end());

        std::stringstream ss;
        ss << std::hex << std::nouppercase << std::setfill('0');
        for (unsigned char byte : hash)
        {
            ss << std::setw(2) << static_cast<int>(byte);
        }

        hashes.push_back(ss.str());
    }

    return hashes;
}

void generate_unpacker(const std::string& output_dir,
                      const std::vector<std::filesystem::path>& files,
                      const std::vector<std::string>& hashes)
{
    namespace fs = std::filesystem;
    fs::path script_path = fs::path(output_dir) / "unpacker.bat";
    
    // 复制7zr.exe到输出目录
    try {
        if (fs::exists("7zr.exe")) {
            fs::copy_file("7zr.exe", 
                         fs::path(output_dir) / "7zr.exe", 
                         fs::copy_options::overwrite_existing);
        }
    } catch (const fs::filesystem_error& e) {
        throw std::runtime_error("Failed to copy 7zr.exe: " + std::string(e.what()));
    }

    // 原有的脚本生成逻辑
    std::ofstream script(script_path);
    if (!script) {
        throw std::runtime_error("Failed to create unpacker script");
    }

    std::string template_str(unpacker_template);
    
    // 生成files数组定义
    std::stringstream files_def;
    for (size_t i = 0; i < files.size(); ++i) {
        files_def << "set files[" << i << "]=" 
                 << files[i].filename().string() << "^|" 
                 << hashes[i] << "\n";
    }
    
    // 替换模板中的占位符
    size_t count = files.size() - 1;
    template_str.replace(
        template_str.find(":: 此处将由C++填充files数组"), 
        strlen(":: 此处将由C++填充files数组"),
        files_def.str());
    
    template_str.replace(
        template_str.find("%COUNT%"),
        strlen("%COUNT%"),
        std::to_string(count));
    
    script << template_str;
}