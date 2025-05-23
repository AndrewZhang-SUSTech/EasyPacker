#pragma once
#include <string_view>

// 批处理解包脚本模板
constexpr std::string_view kUnpackerTemplate = R"!!(
@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion

:: === 文件信息数组（由C++代码自动生成）===
:: GENERATED_FILES_ARRAY_PLACEHOLDER
:: === 文件信息数组结束 ===

echo [信息] 开始文件校验...
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
            ) else (
                echo [成功] %%A 校验通过
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

echo [信息] 所有文件校验成功！

:: 检查7zr.exe是否存在
if not exist "7zr.exe" (
    echo [信息] 未找到7zr.exe，正在下载...
    echo [注意] 杀毒软件可能误报，请选择信任或从 https://www.7-zip.org/a/7zr.exe 手动下载
    powershell -ExecutionPolicy Bypass -Command ^
        "Invoke-WebRequest -Uri 'https://www.7-zip.org/a/7zr.exe' -OutFile '7zr.exe'" || (
        echo [错误] 下载失败，退出
        pause
        exit /b 1
    )
    echo [成功] 7zr.exe 下载完成
)

:: 合并分卷文件
set merge_list=
for /l %%i in (0,1,%VOLUME_COUNT%) do (
    for /F "tokens=1,2 delims=^|" %%A in ("!files[%%i]!") do (
        if not defined merge_list (
            set merge_list=%%A
        ) else (
            set merge_list=!merge_list!+%%A
        )
    )
)

echo [执行] 合并命令: copy /b !merge_list! full_archive.7z
copy /b !merge_list! full_archive.7z >nul
if errorlevel 1 (
    echo [错误] 分卷合并失败
    pause
    exit /b 1
)

:: 解压缩归档文件
echo [信息] 正在解压 full_archive.7z...
7zr.exe x full_archive.7z -bsp1
if errorlevel 1 (
    echo [错误] 解压缩失败
    pause
    exit /b 1
)

:: 清理临时文件
del full_archive.7z >nul 2>&1

echo [成功] 所有操作完成！文件已解压到当前目录
pause
exit /b 0
)!!";