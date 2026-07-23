@echo off
cd /d %~dp0
python mw1989_viewer.py --runtime --backend gpu %*
