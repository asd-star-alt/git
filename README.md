# 高移动性边缘网络：SUMO - ns-3 最小联合仿真

这个工程实现论文的第一阶段：以 100 ms 的统一步长，把 SUMO 车辆位置导出为 CSV，由 WSL 中的 ns-3 回放移动性并产生网络接收日志，最后由 Python 提取扰动强度和稳定性统计。它优先使用共享文件夹，避免 Windows 与 WSL 间 Socket、端口和防火墙配置影响首个实验。

## 目录与数据流

`sumo/` 定义一条 1 km 双向道路；`python/run_sumo_trace.py` 启动 Windows SUMO 和 TraCI；`data/vehicle_trace.csv` 是两个系统共享的交换文件；`ns3/high_mobility_edge.cc` 在 WSL 的 ns-3.42 中回放该轨迹；`python/analyze_stability.py` 输出阶段一统计结果。

## 一次完整运行

在本目录的 Windows PowerShell 依次执行：

```powershell
sumo\prepare_network.ps1
python python\run_sumo_trace.py --speed-kmh 60 --vehicles 12 --duration 60
wsl.exe -d Ubuntu -- bash ns3/run_in_wsl.sh "$(wsl.exe wslpath -a $PWD)"
python python\analyze_stability.py --trace data\vehicle_trace.csv --rx data\received_packets.csv --out outputs
```

`prepare_network.ps1` 要求 `netconvert.exe` 可从命令行找到（通常位于 SUMO 的 `bin` 目录）。启动脚本默认使用已确认的 `/home/songlei/ns-allinone-3.42/ns-3.42`；若日后目录变化，修改 `ns3/run_in_wsl.sh` 顶部的 `NS3_HOME`。

分析步骤还需要 `pandas`、`matplotlib`、`statsmodels`；如尚未安装，在负责分析的 Python 环境运行 `pip install -r python/requirements.txt`。

## 首轮参数扫掠

速度和节点数量是论文第一阶段的自变量。可先运行 30、60、90、120 km/h 与 6、12、18 辆车的组合；每组保留单独的 `data` 和 `outputs` 副本。`summary.csv` 中的 `loss_proxy`、`interarrival_cv`、`adf_pvalue` 和 `bds_pvalue` 可用于绘制“速度 - 扰动强度 - 稳定性”边界。

当前 ns-3 链路采用内置 802.11ad-hoc 网络，确保不依赖额外模块；其中的 `InstallNetwork` 是替换为 mmWave/RIS/NOMA 模型的单一入口。RIS 相位和 NOMA 功率分配应留在 Python 决策层，再以每个同步间隔的链路参数注入 ns-3。

## A2 实时 Socket 闭环

`python/sumo_socket_server.py` 是 Windows 侧服务端：它启动一个 SUMO 单车场景，并且只在收到 ns-3 的 `STEP <time>` 请求时推进 TraCI。`ns3/high_mobility_socket.cc` 是 WSL 侧客户端：每秒请求一次位置、更新车辆移动模型、向 RSU 广播一个 V2I 包，并记录 `send_time,arrive_time,delay_ms,bytes`。

先在 Windows PowerShell 开启服务端（保持窗口不要关闭）：

```powershell
$project = "\\wsl.localhost\Ubuntu-22.04\home\songlei\projects\sumo-ns-3-python-sumo-1"
python "$project\python\sumo_socket_server.py" --duration 60
```

然后在 Ubuntu 的另一终端运行客户端：

```bash
cd ~/projects/sumo-ns-3-python-sumo-1
host_ip=$(ip route | awk '/default/ {print $3; exit}')
bash ns3/run_socket_in_wsl.sh "$PWD" "$host_ip"
```

验证结果：`data/socket_vehicle_trace.csv` 应有每秒的位置更新；`data/v2i_packet_log.csv` 的每一行都包含发送与到达时刻。
