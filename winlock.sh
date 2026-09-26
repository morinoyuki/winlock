#!/usr/bin/env bash
#
# winlock.sh —— WSL2 下管理 Windows 侧 winlock-guard.exe 的小工具
#
# 用法:
#   ./winlock.sh start    启动守护(自动编译; 常驻 Windows 后台)
#   ./winlock.sh stop     停止守护
#   ./winlock.sh restart  重启
#   ./winlock.sh status   查看守护状态与当前拦截情况
#   ./winlock.sh monitor  实时监控(每 0.5s 刷新)
#   ./winlock.sh build    重新编译
#   ./winlock.sh config   显示配置文件路径并提示编辑

set -u

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NAME="winlock-guard"
EXE="$APP_DIR/$NAME.exe"
CONF="$APP_DIR/winlock.conf"
STATUS="$APP_DIR/status.txt"

winpath() { wslpath -w "$1" 2>/dev/null || echo "$1"; }

# 从配置文件取某个键的值(首行)
cfg_val() {
  sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*//p" "$CONF" 2>/dev/null \
    | sed 's/[[:space:]]*$//' | head -1
}

# 取某个键的全部值(支持 game_exe 多行)
cfg_all() {
  sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*//p" "$CONF" 2>/dev/null \
    | sed 's/[[:space:]]*$//'
}

# 全部目标游戏, 逗号连接成一行用于展示
GAME_EXE="$(cfg_all game_exe | paste -sd ', ' -)"
[ -n "$GAME_EXE" ] || GAME_EXE="(未配置)"

usage() {
  sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
}

is_running() {
  # tasklist/taskkill 的 // 参数在 WSL interop 下不可靠, 统一用 PowerShell
  powershell.exe -NoProfile -Command \
    "if (Get-Process -Name '$NAME' -ErrorAction SilentlyContinue) { exit 0 } else { exit 1 }" \
    >/dev/null 2>&1
}

build() {
  if ! command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    echo "错误: 缺少 mingw 交叉编译器, 请先安装:"
    echo "  sudo apt install gcc-mingw-w64-x86-64"
    return 1
  fi
  echo "编译 $EXE ..."
  x86_64-w64-mingw32-gcc -O2 -Wall -mwindows -o "$EXE" "$APP_DIR/guard.c" -lpsapi || return 1
  echo "编译完成"
}

ensure_conf() {
  if [ ! -f "$CONF" ]; then
    cat > "$CONF" <<'EOF'
# winlock 配置文件 (修改后执行 ./winlock.sh restart 生效)
#
# game_exe     目标游戏的 Windows 可执行文件名, 含 .exe, 不区分大小写。
#              支持多个游戏: 多写几行 game_exe, 或一行内用逗号/竖线分隔
# window_title 可选: 窗口标题包含的子串(对全部游戏生效), 留空则只按进程名判断
# poll_ms      前台窗口检测间隔(毫秒)

game_exe = eldenring.exe
game_exe = diablo4.exe
game_exe = monsterhunterwilds.exe
window_title =
poll_ms = 300
EOF
    echo "已生成默认配置: $CONF"
    echo "请把 game_exe 换成你的游戏(可写多行), 然后重新 start"
    return 1
  fi
}

start() {
  ensure_conf || return 1
  if is_running; then
    echo "守护已在运行 (先执行 ./winlock.sh restart 可重新加载配置)"
    return 0
  fi
  [ -f "$EXE" ] || { build || return 1; }

  # Start-Process 在 Windows 侧独立启动, 与 WSL 进程组完全解耦(不会堵塞, 也不随终端关闭而退出)
  powershell.exe -NoProfile -Command \
    "Start-Process '$(winpath "$EXE")' -ArgumentList '--config','$(winpath "$CONF")','--status','$(winpath "$STATUS")'" \
    >/dev/null 2>&1 || {
    # 兜底: 直接后台启动
    ( "$EXE" --config "$(winpath "$CONF")" --status "$(winpath "$STATUS")" >/dev/null 2>&1 & )
  }

  # 等待首次状态写入, 最多 8 秒
  for _ in $(seq 1 16); do
    is_running && break
    sleep 0.5
  done
  if is_running; then
    echo "守护已启动。目标游戏: $GAME_EXE"
    echo "效果: 前台窗口是这些游戏之一时 Win 键被拦截, 切出后自动恢复"
    echo "查看实时状态: ./winlock.sh monitor"
  else
    echo "启动失败, 请检查配置: $CONF"
    return 1
  fi
}

stop() {
  if is_running; then
    powershell.exe -NoProfile -Command "Stop-Process -Name '$NAME' -Force" >/dev/null 2>&1
    echo "守护已停止"
  else
    echo "守护未在运行"
  fi
  rm -f "$STATUS"
}

status() {
  if is_running; then
    echo "守护: 运行中"
    echo "目标: $GAME_EXE"
    if [ -f "$STATUS" ]; then
      echo "------ 当前状态 ------"
      cat "$STATUS"
      focused="$(grep -o '^focused=[01]' "$STATUS" | cut -d= -f2)"
      if [ "$focused" = "1" ]; then
        echo "=> 前方窗口是目标游戏, Win 键正被拦截"
      else
        echo "=> 前方窗口不是目标游戏, Win 键正常"
      fi
    else
      echo "状态文件尚未写入(启动后 1 秒内会自动生成)"
    fi
  else
    echo "守护: 未运行  (./winlock.sh start)"
  fi
}

monitor() {
  echo "实时监控中 (Ctrl+C 退出)  目标: $GAME_EXE"
  while :; do
    status
    sleep 0.5
  done
}

case "${1:-}" in
  start)   start ;;
  stop)    stop ;;
  restart) stop; start ;;
  status)  status ;;
  monitor) monitor ;;
  build)   build ;;
  config)  echo "配置文件: $CONF"; echo "编辑后执行 ./winlock.sh restart" ;;
  *)       usage ;;
esac