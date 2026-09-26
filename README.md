# winlock —— 防止游戏误触 Win 键的小工具

在 **WSL2 里编写/编译/管理，守护程序本体运行在 Windows** 侧。

前台窗口是指定的游戏时，自动拦截 **Win 键**（左/右），防止误触弹出开始菜单或切回桌面；
切出游戏（焦点不在游戏）后 Win 键立即恢复正常。

## 原理

- `guard.c` 用 mingw 交叉编译成 `winlock-guard.exe`（Windows 原生程序）
- 该程序在 Windows 上安装**全局键盘钩子**（`WH_KEYBOARD_LL`），并每 ~300ms 查询一次前台窗口
  （`GetForegroundWindow` → 进程名 / 窗口标题）
- 命中 `winlock.conf` 里配置的游戏 → 吞掉 Win 键按下/抬起事件；否则原样放行

## 快速开始

```bash
cd ~/winlock

# 1. 编辑配置（换成你的游戏，例如 eldenring.exe / diablo4.exe / game.exe）
vim winlock.conf

# 2. 启动（会自动交叉编译，无需手动 build）
./winlock.sh start

# 3. 查看实时状态
./winlock.sh monitor
```

配置示例（**支持多个游戏**）：

```ini
# 目标游戏的 Windows 可执行文件名（含 .exe，不区分大小写）
# 支持多个游戏：多写几行，或一行内用逗号/竖线分隔
#   game_exe = a.exe
#   game_exe = b.exe, c.exe | d.exe
# 未写 .exe 后缀会自动补上（a → a.exe）
game_exe = eldenring.exe
game_exe = diablo4.exe
# 可选：窗口标题包含的子串（对全部游戏生效），留空则只按进程名判断
window_title =
# 前台窗口检测间隔（毫秒）
poll_ms = 300
# 游戏若以管理员身份运行, 此项必须设为 1, 否则按键拦截会静默失效
# （开启后每次启动会弹一次 UAC 授权框; 状态文件 elevated=1 表示已生效）
run_as_admin = 0
```

## 游戏以管理员运行时

如果游戏是右键“以管理员身份运行”的（高完整性进程），Windows 会屏蔽普通权限进程的全局
键盘钩子：**窗口检测仍正常，但按键拦截会静默失效**（status 显示拦截中，实际没吞键）。

对策：把 `winlock.conf` 里的 `run_as_admin` 改为 `1`，让守护自己也以管理员身份运行：

```ini
run_as_admin = 1
```

- 每次启动会弹出一次 UAC 授权框，确认后自动以管理员身份运行（开机自启同理）
- 确认状态：`status` 输出里的 `elevated=1` 表示管理员权限已生效
- 若游戏现在改回普通权限运行了，把此项改回 `0` 即可恢复免弹框模式

## 命令

| 命令        | 说明                                             |
|-------------|--------------------------------------------------|
| `start`     | 启动守护（常驻 Windows 后台，与终端解耦）       |
| `stop`      | 停止守护                                         |
| `restart`   | 修改配置后用它重新加载                           |
| `status`    | 查看守护状态 / 是否正在拦截                      |
| `monitor`   | 每 0.5s 刷新实时状态（Ctrl+C 退出）              |
| `build`     | 手动重新编译（缺少编译器时：`sudo apt install gcc-mingw-w64-x86-64`） |

## 在 Windows 侧独立使用（可选）

把 `winlock-guard.exe` 和 `winlock.conf` 复制到同一个 Windows 文件夹，双击/命令行运行
winlock-guard.exe 即可（配置和状态文件会自动放在 exe 同目录）。
停止：`taskkill /F /IM winlock-guard.exe`。

## Windows 双击版：winlock.bat

把 `winlock.bat` 与 `winlock-guard.exe`、`winlock.conf` 放在**同一文件夹**（建议放到
某个 Windows 盘符目录，双击运行即可）：

| 命令                    | 说明                                        |
|-------------------------|---------------------------------------------|
| 双击 `winlock.bat`       | 交互菜单（启动/停止/状态/监控/安装/卸载） |
| `winlock.bat start`     | 启动守护                                    |
| `winlock.bat stop`      | 停止守护                                    |
| `winlock.bat restart`   | 重启（改配置后用它）                        |
| `winlock.bat status`    | 查看是否正在拦截                            |
| `winlock.bat monitor`   | 每秒刷新实时状态（Ctrl+C 退出）             |
| `winlock.bat install`   | 安装到 `%LOCALAPPDATA%\winlock` 并开机自启 |
| `winlock.bat uninstall` | 停止守护并移除开机自启                      |

说明：
- 不依赖 WSL，双击/在 cmd 里用都行；纯 ASCII，无乱码问题。
- `install` 会注册**开机自启**（Startup 文件夹里多一个 `winlock-autostart.bat`，卸载时自动删除）。
- 守护进程改用一个更稳的方案：拦截基于**前台窗口**，命中配置游戏才吞 Win 键。

## 注意

- **仅建议配合单机/离线游戏使用**。守护程序安装的是全局键盘钩子，某些在线游戏的
  反作弊系统可能不允许这类钩子，被判定违规的风险自负。
- 拦截是**基于前台窗口**的：Alt+Tab 切出去看攻略时 Win 键自动恢复，切回游戏再拦截。
- 状态实时写在 `status.txt`：`focused=1/0` 表示是否命中游戏，`blocking` 同值。