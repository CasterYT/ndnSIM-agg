/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2011-2015  Regents of the University of California.
 *
 * This file is part of ndnSIM. See AUTHORS for complete list of ndnSIM authors and
 * contributors.
 *
 * ndnSIM is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * ndnSIM is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * ndnSIM, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 **/

#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <chrono>

#include "ndn-consumer.hpp"
#include "ns3/ptr.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/packet.h"
#include "ns3/callback.h"
#include "ns3/string.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/integer.h"
#include "ns3/double.h"

#include "ns3/ptr.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/packet.h"
#include "ns3/callback.h"
#include "ns3/string.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/integer.h"
#include "ns3/double.h"

#include "utils/ndn-ns3-packet-tag.hpp"
#include "utils/ndn-rtt-mean-deviation.hpp"

#include <ndn-cxx/lp/tags.hpp>

#include <boost/lexical_cast.hpp>
#include <boost/ref.hpp>

//#include "aggregationTree.hpp"
#include "ModelData.hpp"

#include "src/ndnSIM/apps/algorithm/include/AggregationTree.hpp"
#include "src/ndnSIM/apps/algorithm/utility/utility.hpp"

NS_LOG_COMPONENT_DEFINE("ndn.Consumer");

using namespace std::chrono;

namespace ns3 {
namespace ndn {

NS_OBJECT_ENSURE_REGISTERED(Consumer);



/**
 * Initiate attributes for consumer class, some of them may be used, some are optional
 * Note that currently only use "NodePrefix", ignore "Prefix" for now
 *
 * @return Total TypeId
 */
TypeId
Consumer::GetTypeId(void)
{
  static TypeId tid =
    TypeId("ns3::ndn::Consumer")
      .SetGroupName("Ndn")
      .SetParent<App>()

      .AddAttribute("StartSeq", "Initial sequence number", IntegerValue(0),
                    MakeIntegerAccessor(&Consumer::m_seq), MakeIntegerChecker<int32_t>())
      .AddAttribute("Prefix", "Name of the Interest", StringValue(),
                    MakeStringAccessor(&Consumer::m_interestName), MakeStringChecker())
      .AddAttribute("NodePrefix", "Node prefix", StringValue(),
                    MakeStringAccessor(&Consumer::m_nodeprefix), MakeStringChecker())
      .AddAttribute("LifeTime", "LifeTime for interest packet", StringValue("4s"),
                    MakeTimeAccessor(&Consumer::m_interestLifeTime), MakeTimeChecker())
      .AddAttribute("RetxTimer",
                    "Timeout defining how frequent retransmission timeouts should be checked",
                    StringValue("50ms"),
                    MakeTimeAccessor(&Consumer::GetRetxTimer, &Consumer::SetRetxTimer),
                    MakeTimeChecker())
      .AddTraceSource("LastRetransmittedInterestDataDelay",
                    "Delay between last retransmitted Interest and received Data",
                    MakeTraceSourceAccessor(&Consumer::m_lastRetransmittedInterestDataDelay),
                    "ns3::ndn::Consumer::LastRetransmittedInterestDataDelayCallback")
      .AddTraceSource("FirstInterestDataDelay",
                     "Delay between first transmitted Interest and received Data",
                     MakeTraceSourceAccessor(&Consumer::m_firstInterestDataDelay),
                     "ns3::ndn::Consumer::FirstInterestDataDelayCallback");

  return tid;
}


/**
 * Constructor
 */
Consumer::Consumer()
    : globalSeq(0)
    , globalRound(0)
    , broadcastSync(false)
    , m_rand(CreateObject<UniformRandomVariable>())
    , m_seq(0)
    , total_response_time(0)
    , round(0)
    , totalAggregateTime(0)
    , iteration(0)
{
    m_rtt = CreateObject<RttMeanDeviation>();
}



/**
 * Broadcast the tree construction info to all aggregators
 */
void
Consumer::TreeBroadcast()
{
     const auto& broadcastTree = aggregationTree[0];

     uint32_t seq = 0;

     for (const auto& [parentNode, childList] : broadcastTree) {
         // Don't broadcast to itself
         if (parentNode == m_nodeprefix) {
             continue;
         }

         std::string nameWithType;
         std::string nameType = "initialization";
         nameWithType += "/" + parentNode;
         auto result = getLeafNodes(parentNode, broadcastTree);

         // Construct nameWithType variable for tree broadcast
         for (const auto& [childNode, leaves] : result) {
             std::string name_indication;
             name_indication += childNode + ".";
             for (const auto& leaf : leaves) {
                 name_indication += leaf + ".";
             }
             name_indication.resize(name_indication.size() - 1);
             nameWithType += "/" + name_indication;
         }
         nameWithType += "/" + nameType;

         std::cout << "Node " << parentNode << "'s name is: " << nameWithType << std::endl;
         shared_ptr<Name> newName = make_shared<Name>(nameWithType);
         newName->appendSequenceNumber(seq);
         SendInterest(newName);
     }
 }



 /**
  * Implement the algorithm to compute aggregation tree
  */
void
Consumer::ConstructAggregationTree()
{
    App::ConstructAggregationTree();
    AggregationTree tree(filename);
    int C = 3;
    std::vector<std::string> dataPointNames = Utility::getProducers(filename);
    std::map<std::string, std::vector<std::string>> rawAggregationTree;
    std::vector<std::vector<std::string>> rawSubTree;


    if (tree.aggregationTreeConstruction(dataPointNames, C)) {
        rawAggregationTree = tree.aggregationAllocation;
        rawSubTree = tree.noCHTree;
    } else {
        NS_LOG_DEBUG("Fail to construct aggregation tree!");
        ns3::Simulator::Stop();
    }

    // Get the number of producers
    producerCount = Utility::countProducers(filename);

    // Create producer list
    for (const auto& item : dataPointNames) {
        proList += item + ".";
    }
    proList.resize(proList.size() - 1);


/*     std::cout << "\nAggregation tree: " << std::endl;
     for (const auto& pair : rawAggregationTree) {
         std::cout << pair.first << ": ";
         for (const auto& item : pair.second) {
             std::cout << item << " ";
         }
         std::cout << std::endl;
     }

     std::cout << "\nSub Tree without CH: " << std::endl;
     for (const auto& pair : rawSubTree) {
         for (const auto& item : pair) {
             std::cout << item << " ";
         }
         std::cout << std::endl;
     }*/

    // Create complete "aggregationTree" from raw ones
    aggregationTree.push_back(rawAggregationTree);
    while (!rawSubTree.empty()) {
        const auto& item = rawSubTree[0];
        rawAggregationTree[m_nodeprefix] = item;
        aggregationTree.push_back(rawAggregationTree);
        rawSubTree.erase(rawSubTree.begin());
    }


    int i = 0;
    std::cout << "\nIterate all aggregation tree (including main tree and sub-trees)." << std::endl;
    for (const auto& map : aggregationTree) {
        for (const auto& pair : map) {
            std::cout << pair.first << ": ";
            for (const auto& value : pair.second) {
                std::cout << value << " ";
            }
            std::cout << std::endl;

            // Initialize "broadcastList" for tree broadcasting synchronization
            if (pair.first != m_nodeprefix) {
                broadcastList.push_back(pair.first);
            }

            // Initialize "globalTreeRound" for all rounds (if there're multiple sub-trees)
            if (pair.first == m_nodeprefix) {
                std::vector<std::string> leavesRound;
                std::cout << "Round " << i << " has the following leaf nodes: ";
                for (const auto& leaves : pair.second) {
                    leavesRound.push_back(leaves);
                    std::cout << leaves << " ";
                }
                globalTreeRound.push_back(leavesRound);
                std::cout << std::endl;
            }
        }
        std::cout << "----" << std::endl;  // Separator between maps
        i++;
    }

    // Initialize variables for RTO computation/congestion control
    for (int i = 0; i < globalTreeRound.size(); i++) {
        initRTO[i] = false;
        RTO_Timer[i] = 6 * m_retxTimer;
        m_timeoutThreshold[i] = 6 * m_retxTimer;
        RTT_threshold.push_back(0);
    }

    // Testing, delete later
    NS_LOG_DEBUG("Initialize the following variables.");
    NS_LOG_DEBUG("RTO_Timer: " << RTO_Timer[0].GetMilliSeconds() << " ms");
    NS_LOG_DEBUG("m_timeoutThreshold: " << m_timeoutThreshold[0].GetMilliSeconds() << " ms");

 }



/**
 * Originally defined in ndn::App class, override here. Start the running process of consumer class
 */
void
Consumer::StartApplication() // Called at time specified by Start
{
    // Clear the log file
    std::ofstream file1(RTO_recorder, std::ios::out);
    if (!file1.is_open()) {
        std::cerr << "Failed to open the file: " << RTO_recorder << std::endl;
    }
    file1.close();

    std::ofstream file2(responseTime_recorder, std::ios::out);
    if (!file2.is_open()) {
        std::cerr << "Failed to open the file: " << responseTime_recorder << std::endl;
    }
    file2.close();

    Simulator::Schedule(MilliSeconds(5), &Consumer::RTORecorder, this);

    App::StartApplication();

    // Construct the tree
    ConstructAggregationTree();
    ScheduleNextPacket();
}



/**
 * Originally defined in ndn::App class, override here. Stop the running process of consumer class
 */
void
Consumer::StopApplication() // Called at time specified by Stop
{
    NS_LOG_FUNCTION_NOARGS();
    // cancel periodic packet generation
    Simulator::Cancel(m_sendEvent);
    App::StopApplication();
}



/**
 * Get child nodes for given map and parent node. This function is originally defined in ndn::App class
 * @param key
 * @param treeMap
 * @return A map consisting all child nodes of the parent node
 */
std::map<std::string, std::set<std::string>>
Consumer::getLeafNodes(const std::string& key, const std::map<std::string, std::vector<std::string>>& treeMap)
{
    return App::getLeafNodes(key, treeMap);
}



/**
 * Return round index
 * @param target
 * @return Round index
 */
int
Consumer::findRoundIndex(const std::string& target)
{
    return App::findRoundIndex(globalTreeRound, target);
}



/**
 * Perform aggregation (plus all model parameters together) operation, perform average when this iteration is done
 * @param data
 * @param dataName
 */
void
Consumer::aggregate(const ModelData& data, const std::string& dataName)
{
    // first initialization
    if (sumParameters.find(dataName) == sumParameters.end()){
        sumParameters[dataName] = std::vector<float>(300, 0.0f);
    }

    // Aggregate data
    std::transform(sumParameters[dataName].begin(), sumParameters[dataName].end(), data.parameters.begin(), sumParameters[dataName].begin(), std::plus<float>());
}



/**
 * Get mean average of model parameters for one iteration
 * @param dataName
 * @return Mean average of model parameters
 */
std::vector<float>
Consumer::getMean(const std::string& dataName)
{
    std::vector<float> result;
    if (sumParameters[dataName].empty() || producerCount == 0) {
        NS_LOG_DEBUG("Error when calculating average model, please check!");
        return result;
    }

    for (auto value : sumParameters[dataName]) {
        result.push_back(value / static_cast<float>(producerCount));
    }

    return result;
}



/**
 * Compute the total response time for average computation later
 * @param response_time
 */
void
Consumer::ResponseTimeSum (int64_t response_time)
{
    total_response_time += response_time;
    ++round;
}



/**
 * Compute average response time
 * @return Average response time
 */
int64_t
Consumer::GetResponseTimeAverage() {
    if (round == 0){
        NS_LOG_DEBUG("Error happened when calculating response time!");
        return 0;
    }
    return total_response_time / round;
}



/**
 * Compute total aggregation time for all iterations
 * @param aggregate_time
 */
void
Consumer::AggregateTimeSum (int64_t aggregate_time)
{
    totalAggregateTime += aggregate_time;
    NS_LOG_DEBUG("totalAggregateTime is: " << totalAggregateTime);
    ++iteration;
}



/**
 * Return average aggregation time
 * @return Average aggregation time in certain iterations
 */
int64_t
Consumer::GetAggregateTimeAverage()
{
    if (iteration == 0)
    {
        NS_LOG_DEBUG("Error happened when calculating aggregate time!");
        return 0;
    }

    return totalAggregateTime / iteration;
}



/**
 * Invoked when Nack is triggered
 * @param nack
 */
void
Consumer::OnNack(shared_ptr<const lp::Nack> nack)
{
    /// tracing inside
    App::OnNack(nack);
    NS_LOG_INFO("NACK received for: " << nack->getInterest().getName() << ", reason: " << nack->getReason());
}



/**
 * Triggered when timeout is triggered, timeout is traced using unique interest/data name
 * @param nameString
 */
void
Consumer::OnTimeout(std::string nameString)
{
    shared_ptr<Name> name = make_shared<Name>(nameString);
    SendInterest(name);
}



/**
 * Set initial interval on how long to check timeout
 * @param retxTimer
 */
void
Consumer::SetRetxTimer(Time retxTimer)
{
    m_retxTimer = retxTimer;
    if (m_retxEvent.IsRunning()) {
        Simulator::Remove(m_retxEvent); // slower, but better for memory
    }

    // Schedule new timeout
    //m_timeoutThreshold = 6 * retxTimer;
    NS_LOG_DEBUG("Next interval to check timeout is: " << m_retxTimer.GetMilliSeconds() << " ms");
    m_retxEvent = Simulator::Schedule(m_retxTimer, &Consumer::CheckRetxTimeout, this);
}



/**
 * Get timer for timeout check
 * @return Timer set for timeout checking
 */
Time
Consumer::GetRetxTimer() const
{
  return m_retxTimer;
}



/**
 * When invoked, check whether timeout happened for all packets in timeout list
 */
void
Consumer::CheckRetxTimeout()
{
    Time now = Simulator::Now();

    //NS_LOG_INFO("Check timeout after: " << m_retxTimer.GetMilliSeconds() << " ms");
    //NS_LOG_INFO("Current timeout threshold is: " << m_timeoutThreshold.GetMilliSeconds() << " ms");

    for (auto it = m_timeoutCheck.begin(); it != m_timeoutCheck.end();){
        // Parse the string and extract the first segment, e.g. "agg0", then find out its round
        // ToDo: add logic to parse packet name
        size_t start = it->first.find_first_not_of('/');
        size_t end = it->first.find('/', start);
        std::string segment1;
        if (start != std::string::npos) {
            segment1 = it->first.substr(start, end - start);
        }
        int roundIndex = findRoundIndex(segment1);

        if (now - it->second > m_timeoutThreshold[roundIndex]) {
            std::string name = it->first;
            it = m_timeoutCheck.erase(it);
            OnTimeout(name);
        } else {
            ++it;
        }
    }
    m_retxEvent = Simulator::Schedule(m_retxTimer, &Consumer::CheckRetxTimeout, this);
}



/**
 * Compute new RTO based on response time of recent packets
 * @param resTime
 * @return New RTO
 */
Time
Consumer::RTOMeasurement(int64_t resTime, int roundIndex)
{
    if (!initRTO[roundIndex]) {
        RTTVAR[roundIndex] = resTime / 2;
        SRTT[roundIndex] = resTime;
        NS_LOG_DEBUG("Initialize RTO for round: " << roundIndex);
        initRTO[roundIndex] = true;
    } else {
        RTTVAR[roundIndex] = 0.75 * RTTVAR[roundIndex] + 0.25 * std::abs(SRTT[roundIndex] - resTime); // RTTVAR = (1 - b) * RTTVAR + b * |SRTT - RTTsample|, where b = 0.25
        SRTT[roundIndex] = 0.875 * SRTT[roundIndex] + 0.125 * resTime; // SRTT = (1 - a) * SRTT + a * RTTsample, where a = 0.125
    }
    int64_t RTO = SRTT[roundIndex] + 4 * RTTVAR[roundIndex]; // RTO = SRTT + K * RTTVAR, where K = 4

    return MilliSeconds(RTO);
}



/**
 * When ScheduleNextPacket() is invoked, this function is used to get relevant info and prepare to send interests
 */
void
Consumer::SendPacket()
{
    if (!m_active)
        return;

    // Initialize a full list for aggregation in entire iteration
    if (m_agg_newDataName.find(globalSeq) == m_agg_newDataName.end() && globalSeq != 0) {
        std::vector<std::string> objectProducer;
        std::string token;
        std::istringstream tokenStream(proList);
        char delimiter = '.';
        while (std::getline(tokenStream, token, delimiter)) {
           objectProducer.push_back(token);
        }

        for (const auto& aggTree : aggregationTree) {
            auto initialAllocation = getLeafNodes(m_nodeprefix, aggTree);
            std::vector<std::string> roundChild;

            for (const auto& [child, leaves] : initialAllocation) {
                std::string name_sec1;
                roundChild.push_back(child);

                for (const auto& leaf : leaves) {
                    if (std::find(objectProducer.begin(), objectProducer.end(), leaf) != objectProducer.end()) {
                        name_sec1 += leaf + ".";
                    } else {
                        NS_LOG_DEBUG("Error when initializing mapping!");
                        ns3::Simulator::Stop();
                    }
                }
                name_sec1.resize(name_sec1.size() - 1);
                map_child_nameSec1[child] = name_sec1; // Map for interest splitting later
                m_agg_newDataName[globalSeq].push_back(child); // Map for entire iteration
            }
            map_agg_oldSeq_newName[globalSeq].push_back(roundChild); // Map for each round within one iteration
        }
    }

    // Broadcast aggregation tree in iteration 0
    if (globalSeq == 0) {
        if (!broadcastList.empty()) {
            TreeBroadcast();
        } else {
            NS_LOG_DEBUG("Error when broadcasting tree to aggregators!");
            Simulator::Stop();
        }
        globalSeq = ++m_seq;
    }
    // Start sending actual packets from iteration 1
    else {
        /// "globalRound" represents current round, if there's no sub-tree, there's only one round in each iteration;
        /// otherwise, the number of round represents the number of sub-trees

        // Start computing aggregation time
        if (globalRound == 0)
            aggregateStartTime[globalSeq] = ns3::Simulator::Now();

        // Update to sub-tree when there're many sub-trees apart from main tree
        const auto& aggregationTreeElement = aggregationTree[globalRound];

        // Get child nodes and leaf nodes
        auto childAllocation = getLeafNodes(m_nodeprefix, aggregationTreeElement);

        std::vector<std::string> seqName;

        // Divide interest into several ones and send new ones
        for (const auto& [child, leaves] : childAllocation) {
            std::string name_sec1;
            std::string name;
            std::string nameWithType;

            name_sec1 = map_child_nameSec1[child];
            name = "/" + child + "/" + name_sec1;
            nameWithType = name + "/data";

            // Store the divided interests' name
            //map_agg_oldSeq_newName[globalSeq].push_back(name_sec1);

            // Create vector to store interest name for data retransmission
            seqName.push_back(name);

            shared_ptr<Name> newName = make_shared<Name>(nameWithType);
            newName->appendSequenceNumber(globalSeq);
            SendInterest(newName);
        }

        globalRound++;
        if (globalRound == aggregationTree.size()) {
            globalSeq = ++m_seq;
            globalRound = 0;
        }
    }
    ScheduleNextPacket();
}



/**
 * Invoked in SendPacket() function, used to construct interest packet and send it
 * @param newName
 */
void Consumer::SendInterest(shared_ptr<Name> newName)
{
    if (!m_active)
        return;

    std::string nameWithSeq = newName->toUri();

    // Trace timeout
    m_timeoutCheck[nameWithSeq] = ns3::Simulator::Now();

    // Start response time
    if (currentTime.find(nameWithSeq) == currentTime.end())
        currentTime[nameWithSeq] = ns3::Simulator::Now();

    shared_ptr<Interest> interest = make_shared<Interest>();
    interest->setNonce(m_rand->GetValue(0, std::numeric_limits<uint32_t>::max()));
    interest->setName(*newName);
    interest->setCanBePrefix(false);
    time::milliseconds interestLifeTime(m_interestLifeTime.GetMilliSeconds());
    interest->setInterestLifetime(interestLifeTime);
    NS_LOG_INFO("Sending interest >>>>" << nameWithSeq);
    m_transmittedInterests(interest, this, m_face);
    m_appLink->onReceiveInterest(*interest);

}

///////////////////////////////////////////////////
//          Process incoming packets             //
///////////////////////////////////////////////////
/**
 * Process returned data packets
 * @param data
 */
void
Consumer::OnData(shared_ptr<const Data> data)
{
    if (!m_active)
        return;

    App::OnData(data); // tracing inside
    std::string type = data->getName().get(-2).toUri();
    uint32_t seq = data->getName().at(-1).toSequenceNumber();
    std::string dataName = data->getName().toUri();
    congestionSignalAgg = false;
    congestionSignalCon = false;
    NS_LOG_INFO ("Received content object: " << boost::cref(*data));
    NS_LOG_INFO("The incoming data packet size is: " << data->wireEncode().size());


    // Erase timeout
    if (m_timeoutCheck.find(dataName) != m_timeoutCheck.end())
        m_timeoutCheck.erase(dataName);
    else
        NS_LOG_DEBUG("Suspicious data packet, not exists in timeout list.");

    if (type == "data") {
        std::string seqNum = data->getName().get(-1).toUri();
        std::string name_sec1 = data->getName().get(1).toUri();
        std::string name_sec0 = data->getName().get(0).toUri();

/*        /// Test "m_agg_newDataName", delete later
        NS_LOG_DEBUG("m_agg_newDataName contains: ");
        for (const auto& item : m_agg_newDataName.at(seq)) {
            NS_LOG_DEBUG(item);
        }
        std::cout << std::endl;*/

        // Perform data name matching with interest name
        ModelData modelData;
        auto data_map = map_agg_oldSeq_newName.find(seq);
        auto data_agg = m_agg_newDataName.find(seq);
        if (data_map != map_agg_oldSeq_newName.end() && data_agg != m_agg_newDataName.end()) {

            // Response time computation (RTT)
            if (currentTime.find(dataName) != currentTime.end()){
                responseTime[dataName] = ns3::Simulator::Now() - currentTime[dataName];
                ResponseTimeSum(responseTime[dataName].GetMilliSeconds());
                currentTime.erase(dataName);
                NS_LOG_INFO("Consumer's response time of sequence " << dataName << " is: " << responseTime[dataName].GetMilliSeconds() << " ms");
            }

            // Record response time
            responseTimeRecorder(responseTime[dataName]);

            // Search round index
            const auto& test_data_map = data_map->second;
            int roundIndex = findRoundIndex(name_sec0);
            if (roundIndex == -1) {
                NS_LOG_DEBUG("Error on roundIndex!");
                ns3::Simulator::Stop();
            }
            NS_LOG_INFO("This packet comes from round " << roundIndex);


            // Setup RTT_threshold based on RTT of the first iteration
            if (RTT_threshold[roundIndex] == 0) {
                RTTThresholdMeasure(responseTime[dataName].GetMilliSeconds(), roundIndex);
            } else if (RTT_threshold[roundIndex] != 0 && responseTime[dataName].GetMilliSeconds() > RTT_threshold[roundIndex]) {
                congestionSignalCon = true;
            }

            // Reset RetxTimer and timeout interval
            RTO_Timer[roundIndex] = RTOMeasurement(responseTime[dataName].GetMilliSeconds(), roundIndex);
            NS_LOG_DEBUG("responseTime for name : " << dataName << " is: " << responseTime[dataName].GetMilliSeconds() << " ms");
            NS_LOG_DEBUG("RTT measurement: " << RTO_Timer[roundIndex].GetMilliSeconds() << " ms");
            m_timeoutThreshold[roundIndex] = RTO_Timer[roundIndex];


            // This data exist in the map, perform aggregation
            ///auto& vec = data_map->second;
            auto& aggVec = data_agg->second;
            ///auto vecIt = std::find(vec.begin(), vec.end(), name_sec1);
            auto aggVecIt = std::find(aggVec.begin(), aggVec.end(), name_sec0);
            //auto aggVecIt = std::find(aggVec.begin(), aggVec.end(), name_sec1);

            /// Test "aggVecIt", delete later
            NS_LOG_INFO("aggVecIt element contains: " << name_sec1);

            std::vector<uint8_t> oldbuffer(data->getContent().value(), data->getContent().value() + data->getContent().value_size());

            if (deserializeModelData(oldbuffer, modelData) && aggVecIt != aggVec.end()) {
                aggregate(modelData, seqNum); // Aggregate data payload
                congestionSignalAgg = !modelData.congestedNodes.empty();
                ///vec.erase(vecIt);
                aggVec.erase(aggVecIt);
            } else{
                NS_LOG_INFO("Data name doesn't exist in map_agg_oldSeq_newName, meaning this data packet is duplicate, do nothing!");
                return;
            }



/*            // Check whether aggregation round finished
            if (vec.empty()) {
                NS_LOG_DEBUG("Aggregation round finished. ");
            }*/

            // Judge whether the aggregation iteration has finished
            if (aggVec.empty()) {
                NS_LOG_DEBUG("Aggregation of iteration " << seq << " finished!");

                /// Perform actual aggregation for those data
                std::vector<float> aggregationResult = getMean(seqNum);


                // Calculate aggregate time
                if (aggregateStartTime.find(seq) != aggregateStartTime.end()) {
                    aggregateTime[seq] = ns3::Simulator::Now() - aggregateStartTime[seq];
                    AggregateTimeSum(aggregateTime[seq].GetMilliSeconds());
                    NS_LOG_DEBUG("Iteration " << std::to_string(seq) << " aggregation time is: " << aggregateTime[seq].GetMilliSeconds() << " ms");
                    aggregateStartTime.erase(seq);
                } else {
                    NS_LOG_DEBUG("Error when calculating aggregation time, no reference found for seq " << seq);
                }

            }

            /// Stop simulation
            if (iteration == 200) {
                NS_LOG_DEBUG("Reach 200 iterations, stop!");
                ns3::Simulator::Stop();
                NS_LOG_INFO("The average aggregation time of Consumer in " << iteration << " iteration is: " << GetAggregateTimeAverage() << " ms");
                return;
            }
        } else {
            NS_LOG_DEBUG("Suspicious data packet, not exist in data map.");
            ns3::Simulator::Stop();
        }


    } else if (type == "initialization") {
        std::string destNode = data->getName().get(0).toUri();

        // Update synchronization info
        auto it = std::find(broadcastList.begin(), broadcastList.end(), destNode);
        if (it != broadcastList.end()) {
            broadcastList.erase(it);
            NS_LOG_DEBUG("Node " << destNode << " has received aggregationTree map, erase it from broadcastList");
        }

        // Tree broadcasting synchronization is done
        if (broadcastList.empty()) {
            broadcastSync = true;
            NS_LOG_DEBUG("Synchronization of tree broadcasting finished!");
        }

    }
}



/**
 * Based on RTT of the first iteration, compute their RTT average as threshold, use the threshold for congestion control
 * @param responseTime
 */
void
Consumer::RTTThresholdMeasure(int64_t responseTime, int index)
{
    RTT_threshold_vec[index].push_back(responseTime);
    if (RTT_threshold_vec[index].size() == globalTreeRound[index].size()) {
        int64_t sum = 0;
        for (int64_t item: RTT_threshold_vec[index]) {
            sum += item;
        }
        RTT_threshold[index] = 1.2 * (sum / globalTreeRound[index].size());

        if (RTT_threshold[index] != 0) {
            NS_LOG_INFO("RTT_threshold of round " << index <<" is set as: " << RTT_threshold[index] << " ms");
        } else {
            NS_LOG_INFO("Error! RTT_threshold is set as 0, please check!");
            ns3::Simulator::Stop();
        }

    }
}



/**
 * Record the RTT every 5 ms, store them in a file
 */
void
Consumer::RTORecorder()
{
    // Open the file using fstream in append mode
    std::ofstream file(RTO_recorder, std::ios::app);

    if (!file.is_open()) {
        std::cerr << "Failed to open the file: " << RTO_recorder << std::endl;
        return;
    }

    // Write the response_time to the file, followed by a newline
    file << ns3::Simulator::Now().GetMilliSeconds();
    for (const auto& timer : RTO_Timer) {
        file << " " << timer.second;
    }
    file << std::endl;

    // Close the file
    file.close();
    Simulator::Schedule(MilliSeconds(5), &Consumer::RTORecorder, this);
}



/**
 * Record the response time for each returned packet, store them in a file
 * @param responseTime
 */
void
Consumer::responseTimeRecorder(Time responseTime) {
    // Open the file using fstream in append mode
    std::ofstream file(responseTime_recorder, std::ios::app);

    if (!file.is_open()) {
        std::cerr << "Failed to open the file: " << responseTime_recorder << std::endl;
        return;
    }

    // Write the response_time to the file, followed by a newline
    file << ns3::Simulator::Now().GetMilliSeconds() << " " << responseTime.GetMilliSeconds() << std::endl;

    // Close the file
    file.close();
}




} // namespace ndn
} // namespace ns3
