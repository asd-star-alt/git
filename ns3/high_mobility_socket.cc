#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/seq-ts-header.h"
#include "ns3/wifi-module.h"

#include <arpa/inet.h>
#include <fstream>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <sstream>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("HighMobilitySocket");

static int g_controlFd = -1;
static Ptr<MobilityModel> g_vehicleMobility;
static Ptr<Socket> g_vehicleSocket;
static std::ofstream g_packetLog;
static double g_duration;
static double g_syncPeriod;
static uint32_t g_sequence = 0;

static std::string
ReadLine(int fd)
{
  std::string line;
  char c;
  while (recv(fd, &c, 1, 0) == 1) {
    if (c == '\n') return line;
    line += c;
  }
  return "";
}

static bool
RequestPosition(double time, Vector& position)
{
  std::ostringstream request;
  request << "STEP " << time << "\n";
  const std::string text = request.str();
  if (send(g_controlFd, text.data(), text.size(), 0) < 0) return false;
  std::istringstream reply(ReadLine(g_controlFd));
  std::string type;
  double sampleTime, x, y, speed;
  int active;
  if (!(reply >> type >> sampleTime >> x >> y >> speed >> active) || type != "POS" || !active) return false;
  position = Vector(x, y, 1.5);
  return true;
}

static void
ReceiveV2i(Ptr<Socket> socket)
{
  Address from;
  while (Ptr<Packet> packet = socket->RecvFrom(from)) {
    SeqTsHeader header;
    packet->RemoveHeader(header);
    const double arrived = Simulator::Now().GetSeconds();
    const double sent = header.GetTs().GetSeconds();
    g_packetLog << sent << ',' << arrived << ',' << (arrived - sent) * 1000.0 << ',' << packet->GetSize() << '\n';
  }
}

static void
Synchronise()
{
  Vector position;
  const double now = Simulator::Now().GetSeconds();
  if (!RequestPosition(now, position)) {
    Simulator::Stop();
    return;
  }
  g_vehicleMobility->SetPosition(position);
  SeqTsHeader header;
  header.SetSeq(g_sequence++);
  Ptr<Packet> packet = Create<Packet>(512);
  packet->AddHeader(header);
  g_vehicleSocket->Send(packet);
  if (now + g_syncPeriod <= g_duration) {
    Simulator::Schedule(Seconds(g_syncPeriod), &Synchronise);
  } else {
    Simulator::Stop(MilliSeconds(10));
  }
}

static int
OpenControlConnection(const std::string& host, uint16_t port)
{
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* result = nullptr;
  const std::string service = std::to_string(port);
  if (getaddrinfo(host.c_str(), service.c_str(), &hints, &result) != 0) return -1;
  int fd = -1;
  for (addrinfo* item = result; item; item = item->ai_next) {
    fd = ::socket(item->ai_family, item->ai_socktype, item->ai_protocol);
    if (fd >= 0 && connect(fd, item->ai_addr, item->ai_addrlen) == 0) break;
    if (fd >= 0) close(fd);
    fd = -1;
  }
  freeaddrinfo(result);
  return fd;
}

int
main(int argc, char* argv[])
{
  std::string host, packetLog = "v2i_packet_log.csv";
  uint16_t controlPort = 50007, v2iPort = 9000;
  g_duration = 60.0;
  g_syncPeriod = 1.0;
  CommandLine cmd(__FILE__);
  cmd.AddValue("host", "Windows host IP running sumo_socket_server.py", host);
  cmd.AddValue("controlPort", "TCP control port", controlPort);
  cmd.AddValue("packetLog", "V2I send/arrival log", packetLog);
  cmd.AddValue("duration", "simulation duration in seconds", g_duration);
  cmd.AddValue("syncPeriod", "TraCI synchronisation interval in seconds", g_syncPeriod);
  cmd.Parse(argc, argv);
  if (host.empty()) NS_FATAL_ERROR("Pass --host=<Windows-WSL gateway IP>");
  g_controlFd = OpenControlConnection(host, controlPort);
  if (g_controlFd < 0) NS_FATAL_ERROR("Cannot connect to the Windows TraCI Socket server");

  NodeContainer nodes;
  nodes.Create(3); // RSU 0, RSU 1, vehicle 0
  MobilityHelper mobility;
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install(nodes);
  nodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(500, 0, 6));
  nodes.Get(1)->GetObject<MobilityModel>()->SetPosition(Vector(1000, 0, 6));
  g_vehicleMobility = nodes.Get(2)->GetObject<MobilityModel>();
  g_vehicleMobility->SetPosition(Vector(0, 0, 1.5));

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
  NetDeviceContainer devices = wifi.Install(phy, mac, nodes);
  InternetStackHelper internet;
  internet.Install(nodes);
  Ipv4AddressHelper addresses;
  addresses.SetBase("10.2.0.0", "255.255.255.0");
  addresses.Assign(devices);

  Ptr<Socket> rsuReceiver = Socket::CreateSocket(nodes.Get(0), UdpSocketFactory::GetTypeId());
  rsuReceiver->Bind(InetSocketAddress(Ipv4Address::GetAny(), v2iPort));
  rsuReceiver->SetRecvCallback(MakeCallback(&ReceiveV2i));
  g_vehicleSocket = Socket::CreateSocket(nodes.Get(2), UdpSocketFactory::GetTypeId());
  g_vehicleSocket->SetAllowBroadcast(true);
  g_vehicleSocket->Connect(InetSocketAddress(Ipv4Address("10.2.0.255"), v2iPort));
  g_packetLog.open(packetLog);
  g_packetLog << "send_time,arrive_time,delay_ms,bytes\n";
  Simulator::Schedule(Seconds(g_syncPeriod), &Synchronise);
  Simulator::Stop(Seconds(g_duration + 1.0));
  Simulator::Run();
  const std::string quit = "QUIT\n";
  send(g_controlFd, quit.data(), quit.size(), 0);
  close(g_controlFd);
  g_packetLog.close();
  Simulator::Destroy();
  return 0;
}
