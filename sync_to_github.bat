@echo off
chcp 65001 >nul
echo ========================================
echo   一键同步到 GitHub
echo ========================================
echo.

:: 1. 添加所有变更的文件
echo [1/3] 添加变更文件...
git add .
echo ✅ 完成
echo.

:: 2. 提交（带时间戳方便追踪）
echo [2/3] 提交变更...
git commit -m "Update %date% %time%"
echo ✅ 完成
echo.

:: 3. 推送
echo [3/3] 推送到 GitHub...
git push
if %errorlevel% neq 0 (
    echo ❌ 推送失败，请检查网络或 GitHub 登录状态
    pause
    exit /b 1
)
echo ✅ 推送成功！
echo.
echo ========================================
echo   同步完成！
echo ========================================
timeout /t 2 >nul
