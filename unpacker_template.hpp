#include <string_view>

constexpr std::string_view unpacker_template = R"!!(
@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion

:: === C++自动生成部分开始 ===
:: 此处将由C++填充files数组
:: === C++自动生成部分结束 ===

set errorFlag=0
set i=0

:check_loop
if defined files[%i%] (
    for /F "tokens=1,2 delims=|" %%A in ("!files[%i%]!") do (
        set "filename=%%A"
        set "expected=%%B"
        if not exist "%%A" (
            echo [警告] 文件未找到: %%A
            set errorFlag=1
        ) else (
            echo [验证] 正在校验 %%A 的 SHA256 哈希...
            for /F "skip=1 tokens=*" %%H in ('certutil -hashfile "%%A" SHA256 2^>nul') do (
                set "actual=%%H"
                goto :check_hash
            )
            :check_hash
            set "actual=!actual: =!"
            if /I "!actual!" NEQ "!expected!" (
                echo [错误] 哈希不匹配: %%A
                echo         预期: !expected!
                echo         实际: !actual!
                set errorFlag=1
            )
        )
    )
    set /A i+=1
    goto :check_loop
)

if %errorFlag% NEQ 0 (
    echo.
    echo [失败] 有文件缺失或校验失败，请检查后重试。
    pause
    exit /b 1
)

:: === 检查7zr.exe是否存在，否则下载 ===
if not exist "7zr.exe" (
    echo 未找到 7zr.exe，正在下载...（此处杀毒软件可能会误报，直接信任或者从https://www.7-zip.org/a/7zr.exe下载）
    powershell -ExecutionPolicy Bypass -Command ^
        "Invoke-WebRequest -Uri 'https://www.7-zip.org/a/7zr.exe ' -OutFile '7zr.exe'" || (
        echo 下载失败，退出
        exit /b 1
    )
)

echo 正在合并指定分卷...
set merge_list=
for /l %%i in (0,1,%COUNT%) do (
    for /F "tokens=1,2 delims=^|" %%A in ("!files[%%i]!") do (
        set merge_list=!merge_list!+%%A
    )
)
set "merge_list=!merge_list:~1!"  :: 去掉第一个加号

echo 正在合并分卷: !merge_list!
copy /b !merge_list! full_archive.7z >nul
if errorlevel 1 (
    echo 合并失败
    exit /b 1
)

:: === 解压缩 ===
echo 正在解压 full_archive.7z...
7zr.exe x full_archive.7z -bsp1
if errorlevel 1 (
    echo 解压失败
    exit /b 1
)

echo 所有操作成功完成！
exit /b 0
)!!";