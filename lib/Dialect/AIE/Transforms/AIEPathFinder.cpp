//===- AIEPathfinder.cpp ----------------------------------------*- C++ -*-===//
//
// This file is licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// (c) Copyright 2021 Xilinx Inc.
//
//===----------------------------------------------------------------------===//

#include "aie/Dialect/AIE/Transforms/AIEPathFinder.h"
#include "d_ary_heap.h"

#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_os_ostream.h"

using namespace mlir;
using namespace xilinx;
using namespace xilinx::AIE;

#define DEBUG_TYPE "aie-pathfinder"

LogicalResult DynamicTileAnalysis::runAnalysis(DeviceOp &device) {
  LLVM_DEBUG(llvm::dbgs() << "\t---Begin DynamicTileAnalysis Constructor---\n");
  // find the maxCol and maxRow
  maxCol = 0;
  maxRow = 0;
  for (TileOp tileOp : device.getOps<TileOp>()) {
    maxCol = std::max(maxCol, tileOp.colIndex());
    maxRow = std::max(maxRow, tileOp.rowIndex());
  }

  pathfinder->initialize(maxCol, maxRow, device.getTargetModel());

  // for each flow in the device, add it to pathfinder
  // each source can map to multiple different destinations (fanout)
  for (FlowOp flowOp : device.getOps<FlowOp>()) {
    TileOp srcTile = cast<TileOp>(flowOp.getSource().getDefiningOp());
    TileOp dstTile = cast<TileOp>(flowOp.getDest().getDefiningOp());
    TileID srcCoords = {srcTile.colIndex(), srcTile.rowIndex()};
    TileID dstCoords = {dstTile.colIndex(), dstTile.rowIndex()};
    Port srcPort = {flowOp.getSourceBundle(), flowOp.getSourceChannel()};
    Port dstPort = {flowOp.getDestBundle(), flowOp.getDestChannel()};
    LLVM_DEBUG(llvm::dbgs()
               << "\tAdding Flow: (" << srcCoords.col << ", " << srcCoords.row
               << ")" << stringifyWireBundle(srcPort.bundle) << srcPort.channel
               << " -> (" << dstCoords.col << ", " << dstCoords.row << ")"
               << stringifyWireBundle(dstPort.bundle) << dstPort.channel
               << "\n");
    pathfinder->addFlow(srcCoords, srcPort, dstCoords, dstPort, false);
  }

  for (PacketFlowOp pktFlowOp : device.getOps<PacketFlowOp>()) {
    Region &r = pktFlowOp.getPorts();
    Block &b = r.front();
    Port srcPort, dstPort;
    TileOp srcTile, dstTile;
    TileID srcCoords, dstCoords;
    for (Operation &Op : b.getOperations()) {
      if (auto pktSource = dyn_cast<PacketSourceOp>(Op)) {
        srcTile = dyn_cast<TileOp>(pktSource.getTile().getDefiningOp());
        srcPort = pktSource.port();
        srcCoords = {srcTile.colIndex(), srcTile.rowIndex()};
      } else if (auto pktDest = dyn_cast<PacketDestOp>(Op)) {
        dstTile = dyn_cast<TileOp>(pktDest.getTile().getDefiningOp());
        dstPort = pktDest.port();
        dstCoords = {dstTile.colIndex(), dstTile.rowIndex()};
        LLVM_DEBUG(llvm::dbgs()
                   << "\tAdding Packet Flow: (" << srcCoords.col << ", "
                   << srcCoords.row << ")"
                   << stringifyWireBundle(srcPort.bundle) << srcPort.channel
                   << " -> (" << dstCoords.col << ", " << dstCoords.row << ")"
                   << stringifyWireBundle(dstPort.bundle) << dstPort.channel
                   << "\n");
        // todo: support many-to-one & many-to-many?
        pathfinder->addFlow(srcCoords, srcPort, dstCoords, dstPort, true);
      }
    }
  }

  // add existing connections so Pathfinder knows which resources are
  // available search all existing SwitchBoxOps for exising connections
  for (SwitchboxOp switchboxOp : device.getOps<SwitchboxOp>()) {
    if (!pathfinder->addFixedConnection(switchboxOp))
      return switchboxOp.emitOpError() << "Unable to add fixed connections";
  }

  // all flows are now populated, call the congestion-aware pathfinder
  // algorithm
  // check whether the pathfinder algorithm creates a legal routing
  if (auto maybeFlowSolutions = pathfinder->findPaths(maxIterations))
    flowSolutions = maybeFlowSolutions.value();
  else
    return device.emitError("Unable to find a legal routing");

  // initialize all flows as unprocessed to prep for rewrite
  for (const auto &[pathNode, switchSetting] : flowSolutions) {
    processedFlows[pathNode] = false;
  }

  // fill in coords to TileOps, SwitchboxOps, and ShimMuxOps
  for (auto tileOp : device.getOps<TileOp>()) {
    int col, row;
    col = tileOp.colIndex();
    row = tileOp.rowIndex();
    maxCol = std::max(maxCol, col);
    maxRow = std::max(maxRow, row);
    assert(coordToTile.count({col, row}) == 0);
    coordToTile[{col, row}] = tileOp;
  }
  for (auto switchboxOp : device.getOps<SwitchboxOp>()) {
    int col = switchboxOp.colIndex();
    int row = switchboxOp.rowIndex();
    assert(coordToSwitchbox.count({col, row}) == 0);
    coordToSwitchbox[{col, row}] = switchboxOp;
  }
  for (auto shimmuxOp : device.getOps<ShimMuxOp>()) {
    int col = shimmuxOp.colIndex();
    int row = shimmuxOp.rowIndex();
    assert(coordToShimMux.count({col, row}) == 0);
    coordToShimMux[{col, row}] = shimmuxOp;
  }

  LLVM_DEBUG(llvm::dbgs() << "\t---End DynamicTileAnalysis Constructor---\n");
  return success();
}

TileOp DynamicTileAnalysis::getTile(OpBuilder &builder, int col, int row) {
  if (coordToTile.count({col, row})) {
    return coordToTile[{col, row}];
  }
  auto tileOp = builder.create<TileOp>(builder.getUnknownLoc(), col, row);
  coordToTile[{col, row}] = tileOp;
  maxCol = std::max(maxCol, col);
  maxRow = std::max(maxRow, row);
  return tileOp;
}

SwitchboxOp DynamicTileAnalysis::getSwitchbox(OpBuilder &builder, int col,
                                              int row) {
  assert(col >= 0);
  assert(row >= 0);
  if (coordToSwitchbox.count({col, row})) {
    return coordToSwitchbox[{col, row}];
  }
  auto switchboxOp = builder.create<SwitchboxOp>(builder.getUnknownLoc(),
                                                 getTile(builder, col, row));
  SwitchboxOp::ensureTerminator(switchboxOp.getConnections(), builder,
                                builder.getUnknownLoc());
  coordToSwitchbox[{col, row}] = switchboxOp;
  maxCol = std::max(maxCol, col);
  maxRow = std::max(maxRow, row);
  return switchboxOp;
}

ShimMuxOp DynamicTileAnalysis::getShimMux(OpBuilder &builder, int col) {
  assert(col >= 0);
  int row = 0;
  if (coordToShimMux.count({col, row})) {
    return coordToShimMux[{col, row}];
  }
  assert(getTile(builder, col, row).isShimNOCTile());
  auto switchboxOp = builder.create<ShimMuxOp>(builder.getUnknownLoc(),
                                               getTile(builder, col, row));
  SwitchboxOp::ensureTerminator(switchboxOp.getConnections(), builder,
                                builder.getUnknownLoc());
  coordToShimMux[{col, row}] = switchboxOp;
  maxCol = std::max(maxCol, col);
  maxRow = std::max(maxRow, row);
  return switchboxOp;
}

void Pathfinder::initialize(int maxCol, int maxRow,
                            const AIETargetModel &targetModel) {

  std::map<WireBundle, int> maxChannels;
  auto intraconnect = [&](int col, int row) {
    TileID coords = {col, row};
    SwitchboxConnect sb = {coords};

    const std::vector<WireBundle> bundles = {
        WireBundle::Core,  WireBundle::DMA,  WireBundle::FIFO,
        WireBundle::South, WireBundle::West, WireBundle::North,
        WireBundle::East,  WireBundle::PLIO, WireBundle::NOC,
        WireBundle::Trace, WireBundle::Ctrl};
    for (WireBundle bundle : bundles) {
      // get all ports into current switchbox
      int channels =
          targetModel.getNumSourceSwitchboxConnections(col, row, bundle);
      if (channels == 0 && targetModel.isShimNOCorPLTile(col, row)) {
        // wordaround for shimMux
        channels = targetModel.getNumSourceShimMuxConnections(col, row, bundle);
      }
      for (int channel = 0; channel < channels; channel++) {
        sb.srcPorts.push_back(Port{bundle, channel});
      }
      // get all ports out of current switchbox
      channels = targetModel.getNumDestSwitchboxConnections(col, row, bundle);
      if (channels == 0 && targetModel.isShimNOCorPLTile(col, row)) {
        // wordaround for shimMux
        channels = targetModel.getNumDestShimMuxConnections(col, row, bundle);
      }
      for (int channel = 0; channel < channels; channel++) {
        sb.dstPorts.push_back(Port{bundle, channel});
      }
      maxChannels[bundle] = channels;
    }
    // initialize matrices
    sb.resize();
    for (size_t i = 0; i < sb.srcPorts.size(); i++) {
      for (size_t j = 0; j < sb.dstPorts.size(); j++) {
        auto &pIn = sb.srcPorts[i];
        auto &pOut = sb.dstPorts[j];
        if (targetModel.isLegalTileConnection(col, row, pIn.bundle, pIn.channel,
                                              pOut.bundle, pOut.channel))
          sb.connectivity[i][j] = Connectivity::AVAILABLE;
        else {
          sb.connectivity[i][j] = Connectivity::INVALID;
          if (targetModel.isShimNOCorPLTile(col, row)) {
            // wordaround for shimMux
            auto isBundleInList = [](WireBundle bundle,
                                     std::vector<WireBundle> bundles) {
              return std::find(bundles.begin(), bundles.end(), bundle) !=
                     bundles.end();
            };
            const std::vector<WireBundle> bundles = {
                WireBundle::DMA, WireBundle::NOC, WireBundle::PLIO};
            if (isBundleInList(pIn.bundle, bundles) ||
                isBundleInList(pOut.bundle, bundles))
              sb.connectivity[i][j] = Connectivity::AVAILABLE;
          }
        }
      }
    }
    grid[std::make_pair(coords, coords)] = sb;
  };

  auto interconnect = [&](int col, int row, int targetCol, int targetRow,
                          WireBundle srcBundle, WireBundle dstBundle) {
    SwitchboxConnect sb = {{col, row}, {targetCol, targetRow}};
    for (int channel = 0; channel < maxChannels[srcBundle]; channel++) {
      sb.srcPorts.push_back(Port{srcBundle, channel});
      sb.dstPorts.push_back(Port{dstBundle, channel});
    }
    sb.resize();
    for (size_t i = 0; i < sb.srcPorts.size(); i++) {
      sb.connectivity[i][i] = Connectivity::AVAILABLE;
    }
    grid[std::make_pair(TileID{col, row}, TileID{targetCol, targetRow})] = sb;
  };

  for (int row = 0; row <= maxRow; row++) {
    for (int col = 0; col <= maxCol; col++) {
      maxChannels.clear();
      // connections within the same switchbox
      intraconnect(col, row);

      // connections between switchboxes
      if (row > 0) {
        // from south to north
        interconnect(col, row, col, row - 1, WireBundle::South,
                     WireBundle::North);
      }
      if (row < maxRow) {
        // from north to south
        interconnect(col, row, col, row + 1, WireBundle::North,
                     WireBundle::South);
      }
      if (col > 0) {
        // from east to west
        interconnect(col, row, col - 1, row, WireBundle::West,
                     WireBundle::East);
      }
      if (col < maxCol) {
        // from west to east
        interconnect(col, row, col + 1, row, WireBundle::East,
                     WireBundle::West);
      }
    }
  }
}

// Add a flow from src to dst can have an arbitrary number of dst locations
// due to fanout.
void Pathfinder::addFlow(TileID srcCoords, Port srcPort, TileID dstCoords,
                         Port dstPort, bool isPacketFlow) {
  // check if a flow with this source already exists
  for (auto &[isPkt, src, dsts] : flows) {
    if (src.sb == srcCoords && src.port == srcPort) {
      dsts.emplace_back(PathNode{dstCoords, dstPort});
      return;
    }
  }

  // If no existing flow was found with this source, create a new flow.
  flows.push_back(
      FlowNode{isPacketFlow, PathNode{srcCoords, srcPort},
               std::vector<PathNode>{PathNode{dstCoords, dstPort}}});
}

// Keep track of connections already used in the AIE; Pathfinder algorithm
// will avoid using these.
bool Pathfinder::addFixedConnection(SwitchboxOp switchboxOp) {
  int col = switchboxOp.colIndex();
  int row = switchboxOp.rowIndex();
  TileID coords = {col, row};
  auto &sb = grid[std::make_pair(coords, coords)];
  for (ConnectOp connectOp : switchboxOp.getOps<ConnectOp>()) {
    bool found = false;
    for (size_t i = 0; i < sb.srcPorts.size(); i++) {
      for (size_t j = 0; j < sb.dstPorts.size(); j++) {
        if (sb.srcPorts[i] == connectOp.sourcePort() &&
            sb.dstPorts[j] == connectOp.destPort() &&
            sb.connectivity[i][j] == Connectivity::AVAILABLE) {
          sb.connectivity[i][j] = Connectivity::INVALID;
          found = true;
        }
      }
    }
    if (!found) {
      // could not add such a fixed connection
      return false;
    }
  }
  return true;
}

static constexpr double INF = std::numeric_limits<double>::max();

std::map<PathNode, PathNode> Pathfinder::dijkstraShortestPaths(PathNode src) {
  // Use std::map instead of DenseMap because DenseMap doesn't let you
  // overwrite tombstones.
  std::map<PathNode, double> distance;
  std::map<PathNode, PathNode> preds;
  std::map<PathNode, uint64_t> indexInHeap;
  enum Color { WHITE, GRAY, BLACK };
  std::map<PathNode, Color> colors;
  typedef d_ary_heap_indirect<
      /*Value=*/PathNode, /*Arity=*/4,
      /*IndexInHeapPropertyMap=*/std::map<PathNode, uint64_t>,
      /*DistanceMap=*/std::map<PathNode, double> &,
      /*Compare=*/std::less<>>
      MutableQueue;
  MutableQueue Q(distance, indexInHeap);

  distance[src] = 0.0;
  Q.push(src);
  while (!Q.empty()) {
    src = Q.top();
    Q.pop();

    // get all channels src connects to
    if (channels.count(src) == 0) {
      auto &sb = grid[std::make_pair(src.sb, src.sb)];
      for (size_t i = 0; i < sb.srcPorts.size(); i++) {
        for (size_t j = 0; j < sb.dstPorts.size(); j++) {
          if (sb.srcPorts[i] == src.port &&
              sb.connectivity[i][j] == Connectivity::AVAILABLE) {
            // connections within the same switchbox
            channels[src].push_back(PathNode{src.sb, sb.dstPorts[j]});
          }
        }
      }
      // connections to neighboring switchboxes
      std::vector<std::pair<TileID, Port>> neighbors = {
          {{src.sb.col, src.sb.row - 1}, {WireBundle::North, src.port.channel}},
          {{src.sb.col - 1, src.sb.row}, {WireBundle::East, src.port.channel}},
          {{src.sb.col, src.sb.row + 1}, {WireBundle::South, src.port.channel}},
          {{src.sb.col + 1, src.sb.row}, {WireBundle::West, src.port.channel}}};

      for (const auto &[neighborTile, neighborPort] : neighbors) {
        if (grid.count(std::make_pair(src.sb, neighborTile)) > 0 &&
            src.port.bundle == getConnectingBundle(neighborPort.bundle)) {
          auto &sb = grid[std::make_pair(src.sb, neighborTile)];
          if (std::find(sb.dstPorts.begin(), sb.dstPorts.end(), neighborPort) !=
              sb.dstPorts.end())
            channels[src].push_back({neighborTile, neighborPort});
        }
      }
      std::sort(channels[src].begin(), channels[src].end());
    }

    for (auto &dest : channels[src]) {
      if (distance.count(dest) == 0)
        distance[dest] = INF;
      auto &sb = grid[std::make_pair(src.sb, dest.sb)];
      size_t i = std::distance(
          sb.srcPorts.begin(),
          std::find(sb.srcPorts.begin(), sb.srcPorts.end(), src.port));
      size_t j = std::distance(
          sb.dstPorts.begin(),
          std::find(sb.dstPorts.begin(), sb.dstPorts.end(), dest.port));
      assert(i < sb.srcPorts.size());
      assert(j < sb.dstPorts.size());
      bool relax = distance[src] + sb.demand[i][j] < distance[dest];
      if (colors.count(dest) == 0) {
        // was WHITE
        if (relax) {
          distance[dest] = distance[src] + sb.demand[i][j];
          preds[dest] = src;
          colors[dest] = GRAY;
        }
        Q.push(dest);
      } else if (colors[dest] == GRAY && relax) {
        distance[dest] = distance[src] + sb.demand[i][j];
        preds[dest] = src;
      }
    }
    colors[src] = BLACK;
  }

  return preds;
}

// Perform congestion-aware routing for all flows which have been added.
// Use Dijkstra's shortest path to find routes, and use "demand" as the
// weights. If the routing finds too much congestion, update the demand
// weights and repeat the process until a valid solution is found. Returns a
// map specifying switchbox settings for all flows. If no legal routing can be
// found after maxIterations, returns empty vector.
std::optional<std::map<PathNode, SwitchSettings>>
Pathfinder::findPaths(const int maxIterations) {
  LLVM_DEBUG(llvm::dbgs() << "\t---Begin Pathfinder::findPaths---\n");
  std::map<PathNode, SwitchSettings> routingSolution;
  // initialize all Channel histories to 0
  for (auto &[_, sb] : grid) {
    for (size_t i = 0; i < sb.srcPorts.size(); i++) {
      for (size_t j = 0; j < sb.dstPorts.size(); j++) {
        sb.usedCapacity[i][j] = 0;
        sb.overCapacity[i][j] = 0;
      }
    }
  }

  int iterationCount = -1;
  int illegalEdges = 0;
  int totalPathLength = 0;
  do {
    // if reach maxIterations, throw an error since no routing can be found
    if (++iterationCount >= maxIterations) {
      LLVM_DEBUG(llvm::dbgs()
                 << "\t\tPathfinder: maxIterations has been exceeded ("
                 << maxIterations
                 << " iterations)...unable to find routing for flows.\n");
      return std::nullopt;
    }

    LLVM_DEBUG(llvm::dbgs() << "\t\t---Begin findPaths iteration #"
                            << iterationCount << "---\n");
    // update demand at the beginning of each iteration
    for (auto &[_, sb] : grid) {
      sb.updateDemand();
    }

    // "rip up" all routes
    illegalEdges = 0;
    totalPathLength = -1;
    routingSolution.clear();
    for (auto &[_, sb] : grid) {
      for (size_t i = 0; i < sb.srcPorts.size(); i++) {
        for (size_t j = 0; j < sb.dstPorts.size(); j++) {
          sb.usedCapacity[i][j] = 0;
          sb.packetFlowCount[i][j] = 0;
        }
      }
    }

    // for each flow, find the shortest path from source to destination
    // update used_capacity for the path between them
    for (const auto &[isPkt, src, dsts] : flows) {
      // Use dijkstra to find path given current demand from the start
      // switchbox; find the shortest paths to each other switchbox. Output is
      // in the predecessor map, which must then be processed to get
      // individual switchbox settings
      std::set<PathNode> processed;
      std::map<PathNode, PathNode> preds = dijkstraShortestPaths(src);

      // trace the path of the flow backwards via predecessors
      // increment used_capacity for the associated channels
      SwitchSettings switchSettings;
      // set the input bundle for the source endpoint
      switchSettings[src.sb].src = src.port;
      processed.insert(src);
      for (auto endPoint : dsts) {
        auto curr = endPoint;
        // set the output bundle for this destination endpoint
        switchSettings[endPoint.sb].dsts.insert(endPoint.port);
        // trace backwards until a vertex already processed is reached
        while (!processed.count(curr)) {
          auto &sb = grid[std::make_pair(preds[curr].sb, curr.sb)];
          size_t i =
              std::distance(sb.srcPorts.begin(),
                            std::find(sb.srcPorts.begin(), sb.srcPorts.end(),
                                      preds[curr].port));
          size_t j = std::distance(
              sb.dstPorts.begin(),
              std::find(sb.dstPorts.begin(), sb.dstPorts.end(), curr.port));
          assert(i < sb.srcPorts.size());
          assert(j < sb.dstPorts.size());
          if (isPkt) {
            sb.packetFlowCount[i][j]++;
            // maximum packet stream per channel
            if (sb.packetFlowCount[i][j] >= MAX_PACKET_STREAM_CAPACITY) {
              sb.packetFlowCount[i][j] = 0;
              sb.usedCapacity[i][j]++;
            }
          } else {
            sb.packetFlowCount[i][j] = 0;
            sb.usedCapacity[i][j]++;
          }
          // if at capacity, bump demand to discourage using this Channel
          // this means the order matters!
          sb.bumpDemand(i, j);
          if (preds[curr].sb == curr.sb) {
            switchSettings[preds[curr].sb].src = preds[curr].port;
            switchSettings[curr.sb].dsts.insert(curr.port);
          }
          processed.insert(curr);
          curr = preds[curr];
        }
      }
      // add this flow to the proposed solution
      routingSolution[src] = switchSettings;
    }

    for (auto &[_, sb] : grid) {
      for (size_t i = 0; i < sb.srcPorts.size(); i++) {
        for (size_t j = 0; j < sb.dstPorts.size(); j++) {
          // fix used capacity for packet flows
          if (sb.packetFlowCount[i][j] > 0) {
            sb.packetFlowCount[i][j] = 0;
            sb.usedCapacity[i][j]++;
          }
          // check that every channel does not exceed max capacity
          if (sb.usedCapacity[i][j] > MAX_CIRCUIT_STREAM_CAPACITY) {
            sb.overCapacity[i][j]++;
            illegalEdges++;
            LLVM_DEBUG(llvm::dbgs()
                       << "\t\t\tToo much capacity on (" << sb.srcTile.col
                       << "," << sb.srcTile.row << ") " << sb.srcPorts[i].bundle
                       << sb.srcPorts[i].channel << " -> (" << sb.dstTile.col
                       << "," << sb.dstTile.row << ") " << sb.dstPorts[j].bundle
                       << sb.dstPorts[j].channel
                       << ", used_capacity = " << sb.usedCapacity[i][j]
                       << ", demand = " << sb.demand[i][j]
                       << ", over_capacity_count = " << sb.overCapacity[i][j]
                       << "\n");
          }
          // calculate total path length (across switchboxes)
          if (sb.srcTile != sb.dstTile) {
            totalPathLength += sb.usedCapacity[i][j];
          }
        }
      }
    }

#ifndef NDEBUG
    for (const auto &[pathNode, switchSetting] : routingSolution) {
      LLVM_DEBUG(llvm::dbgs() << "\t\t\tFlow starting at (" << pathNode.sb.col
                              << "," << pathNode.sb.row << "):\t");
      LLVM_DEBUG(llvm::dbgs() << switchSetting);
    }
#endif
    LLVM_DEBUG(llvm::dbgs()
               << "\t\t---End findPaths iteration #" << iterationCount
               << " , illegal edges count = " << illegalEdges
               << ", total path length = " << totalPathLength << "---\n");
  } while (illegalEdges >
           0); // continue iterations until a legal routing is found

  LLVM_DEBUG(llvm::dbgs() << "\t---End Pathfinder::findPaths---\n");
  return routingSolution;
}
