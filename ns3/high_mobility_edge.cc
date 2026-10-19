#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("HighMobilityEdge");

struct Sample { double time; double x; double y; };
static std::ofstream g_rx;

static void
ReceivePacket(Ptr<const Packet> packet, const Address&, const Address&)
{
  g_rx << Simulator::Now().GetSeconds() << ',' << packet->GetSize() << '\n';
}

static std::map<std::string, std::vector<Sample>>
ReadTrace(const std::string& filename)
{
  std::ifstream input(filename);
  if (!input) NS_FATAL_ERROR("Cannot read trace: " << filename);
  std::string line;
  std::getline(input, line); // header
  std::map<std::string, std::vector<Sample>> traces;
  while (std::getline(input, line)) {
    std::stringstream row(line);
    std::string time, id, x, y, speed;
    if (!std::getline(row, time, ',') || !std::getline(row, id, ',') ||
        !std::getline(row, x, ',') || !std::getline(row, y, ',') || !std::getline(row, speed, ',')) continue;
    traces[id].push_back({std::stod(time), std::stod(x), std::stod(y)});
  }
  return traces;
}

int
main(int argc, char* argv[])
{
  std::string trace = "vehicle_trace.csv", rx = "received_packets.csv", flowmon = "flowmon.xml";
  CommandLine cmd(__FILE__);
  cmd.AddValue("trace", "SUMO trajectory CSV", trace);
  cmd.AddValue("rx", "received-packet CSV output", rx);
  cmd.AddValue("flowmon", "FlowMonitor XML output", flowmon);
  cmd.Parse(argc, argv);

  auto traces = ReadTrace(trace);
  if (traces.empty()) NS_FATAL_ERROR("Trace contains no vehicle samples");
  double stop = 0.0;
  for (const auto& [id, samples] : traces) for (const auto& sample : samples) stop = std::max(stop, sample.time);

  NodeContainer all;
  all.Create(2 + traces.size()); // RSU 0 is uplink receiver; RSU 1 reserves the second edge site.
  MobilityHelper mobility;
  mobility.SetMobilityModel("ns3::WaypointMobilityModel");
  mobility.Install(all);
  all.Get(0)->GetObject<WaypointMobilityModel>()->AddWaypoint(Waypoint(Seconds(0), Vector(500, 0, 6)));
  all.Get(1)->GetObject<WaypointMobilityModel>()->AddWaypoint(Waypoint(Seconds(0), Vector(1000, 0, 6)));
  uint32_t node = 2;
  for (const auto& [id, samples] : traces) {
    auto model = all.Get(node++)->GetObject<WaypointMobilityModel>();
    for (const auto& sample : samples) model->AddWaypoint(Waypoint(Seconds(sample.time), Vector(sample.x, sample.y, 1.5)));
  }

  // Deterministic 200 m RSU cell for the baseline experiment.  A later mmWave/RIS
  // model replaces this channel while keeping the mobility and application layers.
  YansWifiChannelHelper channel;
  channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
  channel.AddPropagationLoss("ns3::RangePropagationLossModel", "MaxRange", DoubleValue(200.0));
  YansWifiPhyHelper phy;
  phy.SetChannel(channel.Create());
  phy.Set("TxPowerStart", DoubleValue(20.0));
  phy.Set("TxPowerEnd", DoubleValue(20.0));
  WifiHelper wifi;
  wifi.SetStandard(WIFI_STANDARD_80211ac);
  WifiMacHelper mac;
  mac.SetType("ns3::AdhocWifiMac");
  NetDeviceContainer devices = wifi.Install(phy, mac, all);
  InternetStackHelper internet;
  internet.Install(all);
  Ipv4AddressHelper addresses;
  addresses.SetBase("10.1.0.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = addresses.Assign(devices);

  uint16_t port = 9000;
  UdpServerHelper server(port);
  auto serverApps = server.Install(all.Get(0));
  serverApps.Start(Seconds(0));
  serverApps.Stop(Seconds(stop + 0.5));
  auto udpServer = DynamicCast<UdpServer>(serverApps.Get(0));
  g_rx.open(rx);
  g_rx << "time_s,bytes\n";
  udpServer->TraceConnectWithoutContext("RxWithAddresses", MakeCallback(&ReceivePacket));

  for (uint32_t i = 2; i < all.GetN(); ++i) {
    // Broadcast avoids an ARP failure while a vehicle is initially outside the
    // RSU cell.  The receiver records packets only when the vehicle enters range.
    UdpClientHelper client(Ipv4Address("10.1.0.255"), port);
    client.SetAttribute("Interval", TimeValue(MilliSeconds(20)));
    client.SetAttribute("PacketSize", UintegerValue(512));
    client.SetAttribute("MaxPackets", UintegerValue(static_cast<uint32_t>(stop / 0.02)));
    auto app = client.Install(all.Get(i));
    app.Start(Seconds(0.2));
    app.Stop(Seconds(stop));
  }
  FlowMonitorHelper monitorHelper;
  Ptr<FlowMonitor> monitor = monitorHelper.InstallAll();
  Simulator::Stop(Seconds(stop + 0.5));
  Simulator::Run();
  monitor->CheckForLostPackets();
  monitor->SerializeToXmlFile(flowmon, true, true);
  g_rx.close();
  Simulator::Destroy();
  return 0;
}
