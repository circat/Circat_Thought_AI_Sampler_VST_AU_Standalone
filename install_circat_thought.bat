@echo off
setlocal EnableExtensions DisableDelayedExpansion
set "ROOT=%~dp0"
set "RUNTIME=%LOCALAPPDATA%\Circat\CircatThought\StableAudioOpen"
set "LOGDIR=%ROOT%logs"
if not exist "%LOGDIR%" mkdir "%LOGDIR%"
set "LOG=%LOGDIR%\install.log"
echo Circat Thought - Stable Audio Open installer
where uv >nul 2>&1 || (echo ERROR: install uv first: winget install --id Astral-sh.uv --exact& exit /b 1)
where nvidia-smi >nul 2>&1 && (echo NVIDIA GPU detected.& nvidia-smi --query-gpu=name,memory.total,driver_version --format=csv,noheader) || echo WARNING: NVIDIA GPU not detected; CPU generation is slow.
if not exist "%RUNTIME%\.venv\Scripts\python.exe" uv venv "%RUNTIME%\.venv" --python 3.11 >>"%LOG%" 2>&1 || (echo ERROR: could not create Python 3.11 environment.& exit /b 1)
echo Installing PyTorch CUDA and Stable Audio Open...
uv pip install --python "%RUNTIME%\.venv\Scripts\python.exe" torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu124 >>"%LOG%" 2>&1 || (echo ERROR: PyTorch install failed; see %LOG%& exit /b 1)
uv pip install --python "%RUNTIME%\.venv\Scripts\python.exe" stable-audio-tools huggingface_hub >>"%LOG%" 2>&1 || (echo ERROR: Stable Audio install failed; see %LOG%& exit /b 1)
echo Accept the Stable Audio Open license on Hugging Face before continuing.
set /p "HF_TOKEN=Hugging Face read token (blank to log in later): "
if not "%HF_TOKEN%"=="" "%RUNTIME%\.venv\Scripts\hf.exe" auth login --token "%HF_TOKEN%" >>"%LOG%" 2>&1
echo Installation complete. Run backend\start_stable_audio.bat, then press LOAD MODEL in the UI.
exit /b 0
