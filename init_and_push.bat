@echo off
chcp 65001 >nul
echo ========================================
echo   初始化 Git 并推送到 GitHub
echo ========================================
echo.

:: 1. 初始化 Git 仓库
echo [1/5] 初始化 Git 仓库...
git init
if %errorlevel% neq 0 (
    echo ❌ git init 失败，请检查 git 是否已安装并加入 PATH
    pause
    exit /b 1
)
echo ✅ 完成
echo.

:: 2. 添加所有文件
echo [2/5] 添加所有文件...
git add .
echo ✅ 完成
echo.

:: 3. 提交
echo [3/5] 创建初始提交...
git commit -m "Initial commit"
if %errorlevel% neq 0 (
    echo ⚠️ 提交可能没有新文件或已存在提交，继续...
)
echo ✅ 完成
echo.

:: 4. 关联远程仓库
echo [4/5] 关联远程仓库...
git remote remove origin 2>nul
git remote add origin https://github.com/gujiu47-2KPJ/Git_AI.git
echo ✅ 完成
echo.

:: 5. 推送到 GitHub
echo [5/5] 推送到 GitHub...
git push -u origin main
if %errorlevel% neq 0 (
    echo ⚠️ main 分支推送失败，尝试 master 分支...
    git push -u origin master
    if %errorlevel% neq 0 (
        echo ❌ 推送失败，请检查：
        echo    1. GitHub 仓库是否已创建
        echo    2. 是否有推送权限（可能需要登录）
        pause
        exit /b 1
    )
)
echo ✅ 推送成功！
echo.
echo ========================================
echo   全部完成！
echo ========================================
pause
