# Third-Party Notices

ShaderPlayer's installer ships the components listed below alongside the
application itself. Each entry names the component, its licence, and where
its own source is obtained.

## FFmpeg

FFmpeg 8.x shared build (`avcodec-62.dll`, `avdevice-62.dll`, `avfilter-11.dll`,
`avformat-62.dll`, `avutil-60.dll`, `swresample-6.dll`, `swscale-9.dll`).

- **Licence**: GPLv3
- Built with `--enable-gpl --enable-version3 --enable-libx264`
- Source: https://ffmpeg.org/download.html

## Qt

Qt 6.9 (`Qt6Core`, `Qt6Gui`, `Qt6Widgets`, `Qt6Network`, `Qt6Svg`, and the
associated plugin directories).

- **Licence**: LGPLv3
- Dynamically linked. The DLLs ship unmodified, and relinking the application
  against a different Qt build is possible by replacing them.
- Source: https://download.qt.io/

## KSyntaxHighlighting

`KF6SyntaxHighlighting.dll`, part of KDE Frameworks.

- **Licence**: LGPLv2+
- Dynamically linked. The DLL ships unmodified, and relinking the application
  against a different build is possible by replacing it.
- Source: https://invent.kde.org/frameworks/syntax-highlighting

## Spout2

- **Licence**: BSD 2-Clause
- Statically linked
- Source: https://github.com/leadedge/Spout2

## miniaudio

- **Licence**: Public domain (Unlicense) or MIT-0, at your option
- Statically linked (single-header)
- Source: https://github.com/mackron/miniaudio

## KissFFT

- **Licence**: BSD 3-Clause
- Statically linked
- Source: https://github.com/mborgerding/kissfft

## nlohmann/json

- **Licence**: MIT
- Header-only, compiled into the application
- Source: https://github.com/nlohmann/json
