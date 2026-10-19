$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $here
try {
    netconvert --node-files nodes.nod.xml --edge-files edges.edg.xml --output-file edge_network.net.xml
    Write-Host 'SUMO network created: sumo/edge_network.net.xml'
} finally {
    Pop-Location
}
