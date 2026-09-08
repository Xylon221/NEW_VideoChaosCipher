#!/usr/bin/env bash
set -euo pipefail

echo "== Platform =="
uname -a

echo
if command -v ffmpeg >/dev/null 2>&1; then
  echo "== FFmpeg hardware-related decoders =="
  ffmpeg -hide_banner -decoders 2>/dev/null | grep -E 'rkmpp|v4l2m2m|vaapi|cuda|qsv' || echo "No hardware decoder names found"

  echo
  echo "== FFmpeg hardware-related encoders =="
  ffmpeg -hide_banner -encoders 2>/dev/null | grep -E 'rkmpp|v4l2m2m|vaapi|cuda|qsv|x264' || echo "No hardware encoder names found"
else
  echo "ffmpeg not found"
fi

echo
if command -v v4l2-ctl >/dev/null 2>&1; then
  echo "== V4L2 devices =="
  v4l2-ctl --list-devices || true
else
  echo "v4l2-ctl not found; install v4l-utils on RK3588 for richer inspection"
fi

echo
printf '== Device nodes ==\n'
ls -l /dev/mpp* /dev/rga* /dev/video* 2>/dev/null || echo "No /dev/mpp*, /dev/rga*, or /dev/video* nodes visible"
