# NexusIM Qt 5 desktop client

This directory is a standalone Windows client project. It deliberately does not
link Muduo, MySQL, Redis, ZooKeeper, or MPRPC. It communicates with an existing
ChatServer or Nginx Stream endpoint using the Stage 1 wire format: a four-byte
big-endian length followed by one UTF-8 JSON object.

Stage 5 adds a non-blocking DeepSeek AI page. Stage 6 adds real friend and
group management, member lists, distributed group chat, offline group-message
restore and per-group unread indicators through `SocialController`. The Windows client talks
only to ChatServer; API credentials and HTTPS model calls stay in the Linux
AIService. AI requests use correlated message IDs 14/15 and a 90-second UI
timeout.

## Configure and build

Run from the repository root in a VS2019 x64 developer shell:

```powershell
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$qt = "E:\Qt\Qt5.12.8\5.12.8\msvc2017_64"

& $cmake -S client/qt -B build/qt-client `
  -G "Visual Studio 16 2019" -A x64 `
  -DCMAKE_PREFIX_PATH="$qt" `
  -DNEXUSIM_QT_BUILD_TESTS=ON

& $cmake --build build/qt-client --config Release --parallel
```

The executable is `build/qt-client/bin/Release/NexusIMClient.exe`.

## Test

```powershell
$env:Path = "E:\Qt\Qt5.12.8\5.12.8\msvc2017_64\bin;$env:Path"
ctest --test-dir build/qt-client -C Release --output-on-failure
python client/qt/tests/source_guards.py
```

## Package and run

The packaging script refuses to overwrite an existing directory:

```powershell
powershell -ExecutionPolicy Bypass -File client/qt/package.ps1 -Configuration Release
dist\NexusIMClient-Release\NexusIMClient.exe --host 192.168.1.100 --port 8000
```

For the final reproducible Stage 7 package (build, CTest, `windeployqt`,
manifest, ZIP and SHA-256):

```powershell
powershell -ExecutionPolicy Bypass -File client/qt/release.ps1 `
  -Version 1.0.0-stage7 `
  -QtRoot E:\Qt\Qt5.12.8\5.12.8\msvc2017_64
```

The resulting ZIP is written below `dist/`. It contains no server
password, DeepSeek API key, or fixed deployment address.

Use port 6001 or 6002 for deterministic node testing, and port 8000 for the
Nginx Stream entry. Settings can also be changed in the login window. Running
the executable twice is supported; no single-instance lock is used.

See `docs/stage4-protocol-matrix.md`, `docs/stage4-qt-client.md`, and
`docs/stage5-implementation.md` for protocol, feature, test, and reliability
details.
