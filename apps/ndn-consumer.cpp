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

namespace ns3 {
namespace ndn {

NS_OBJECT_ENSURE_REGISTERED(Consumer);



/**
 * Initiate attributes for consumer class, some of them may be used, some are optional
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
    : RTT_threshold(0)
    , globalSeq(0)
    , roundSendInterest(0)
    , m_rand(CreateObject<UniformRandomVariable>())
    , m_seq(0)
    , total_response_time(0)
    , SRTT(0)
    , RTTVAR(0)
    , roundRTT(0)
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
             numChild = childList.size();
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
     int C = 10;
     std::vector<std::string> dataPointNames = Utility::getProducers(filename);
     std::map<std::string, std::vector<std::string>> rawAggregationTree;
     std::vector<std::vector<std::string>> rawSubTree;


     if (tree.aggregationTreeConstruction(dataPointNames, C)) {
         rawAggregationTree = tree.aggregationAllocation;
         rawSubTree = tree.noCHTree;
     }

     // Get the number of producers
     producerCount = Utility::countProducers(filename);
     NS_LOG_INFO("The number of producers is: " << producerCount);

     for (const auto& item : dataPointNames) {
         proList += item + ".";
     }
     proList.resize(proList.size() - 1);
     NS_LOG_INFO("proList: " << proList);

     std::cout << "Aggregation tree: " << std::endl;
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
     }

     aggregationTree.push_back(rawAggregationTree);
     while (!rawSubTree.empty()) {
         const auto& item = rawSubTree[0];
         rawAggregationTree[m_nodeprefix] = item;
         aggregationTree.push_back(rawAggregationTree);
         rawSubTree.erase(rawSubTree.begin());
     }

     std::cout << "Test aggregationTree variable." << std::endl;
     for (const auto& map : aggregationTree) {
         for (const auto& pair : map) {
             std::cout << pair.first << ": ";
             for (const auto& value : pair.second) {
                 std::cout << value << " ";
             }
             std::cout << std::endl;
         }
         std::cout << "----" << std::endl;  // Separator between maps
     }
 }



/**
 * Originally defined in ndn::App class, override here. Start the running process of consumer class
 */
void
Consumer::StartApplication() // Called at time specified by Start
{
    // Open and immediately close the file in write mode to clear it
    std::ofstream file1(RTT_recorder, std::ios::out);
    if (!file1.is_open()) {
        std::cerr << "Failed to open the file: " << RTT_recorder << std::endl;
    }
    file1.close();

    std::ofstream file2(responseTime_recorder, std::ios::out);
    if (!file2.is_open()) {
        std::cerr << "Failed to open the file: " << responseTime_recorder << std::endl;
    }
    file2.close();

    Simulator::Schedule(MilliSeconds(5), &Consumer::RTTRecorder, this);

    // Construct the tree
    ConstructAggregationTree();
    App::StartApplication();
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
    m_timeoutThreshold = 6 * retxTimer;
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

    NS_LOG_DEBUG("Check timeout after: " << m_retxTimer.GetMilliSeconds() << " ms");
    NS_LOG_DEBUG("Current timeout threshold is: " << m_timeoutThreshold.GetMilliSeconds() << " ms");

    for (auto it = m_timeoutCheck.begin(); it != m_timeoutCheck.end();){
        if (now - it->second > m_timeoutThreshold) {
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
Consumer::RTOMeasurement(int64_t resTime)
{
    if (roundRTT == 0) {
        RTTVAR = resTime / 2;
        SRTT = resTime;
    } else {
        RTTVAR = 0.75 * RTTVAR + 0.25 * std::abs(SRTT - resTime); // RTTVAR = (1 - b) * RTTVAR + b * |SRTT - RTTsample|, where b = 0.25
        SRTT = 0.875 * SRTT + 0.125 * resTime; // SRTT = (1 - a) * SRTT + a * RTTsample, where a = 0.125
    }
    roundRTT++;
    int64_t RTO = SRTT + 4 * RTTVAR; // RTO = SRTT + K * RTTVAR, where K = 4

    return MilliSeconds(RTO);
}



/**
 * When ScheduleNextPacket() is invoked, this function is used to get relevant info and send out interests
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

            for (const auto& [child, leaves] : initialAllocation) {
                std::string name_sec1;
                for (const auto& leaf : leaves) {
                    if (std::find(objectProducer.begin(), objectProducer.end(), leaf) != objectProducer.end()) {
                        name_sec1 += leaf + ".";
                    }
                }
                name_sec1.resize(name_sec1.size() - 1);
                map_child_nameSec1[child] = name_sec1;
                m_agg_newDataName[globalSeq].push_back(name_sec1);
            }
        }
    }

    if (globalSeq == 0) {
        TreeBroadcast();
        globalSeq = ++m_seq;
    } else {

        // Start computing aggregation time
        if (roundSendInterest == 0)
            aggregateStartTime[globalSeq] = ns3::Simulator::Now();

        // Update to sub-tree when there're many sub-trees apart from main tree
        const auto& aggregationTreeElement = aggregationTree[roundSendInterest];

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
            map_agg_oldSeq_newName[globalSeq].push_back(name_sec1);

            // Create vector to store interest name for data retransmission
            seqName.push_back(name);

            shared_ptr<Name> newName = make_shared<Name>(nameWithType);
            newName->appendSequenceNumber(globalSeq);
            SendInterest(newName);
        }

        roundSendInterest++;
        if (roundSendInterest == aggregationTree.size()) {
            globalSeq = ++m_seq;
            roundSendInterest = 0;
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
    NS_LOG_INFO ("Received content object: " << boost::cref(*data));

    // Erase timeout
    if (m_timeoutCheck.find(dataName) != m_timeoutCheck.end())
        m_timeoutCheck.erase(dataName);
    else
        NS_LOG_DEBUG("Suspicious data packet, not exists in timeout list.");

    if (type == "data") {
        std::string seqNum = data->getName().get(-1).toUri();
        std::string name_sec1 = data->getName().get(1).toUri();

        ModelData modelData;
        auto data_map = map_agg_oldSeq_newName.find(seq);
        auto data_agg = m_agg_newDataName.find(seq);
        if (data_map != map_agg_oldSeq_newName.end() && data_agg != m_agg_newDataName.end()) {

            // This data exist in the map, perform aggregation
            auto& vec = data_map->second;
            auto& aggVec = data_agg->second;
            auto vecIt = std::find(vec.begin(), vec.end(), name_sec1);
            auto aggVecIt = std::find(aggVec.begin(), aggVec.end(), name_sec1);

            std::vector<uint8_t> oldbuffer(data->getContent().value(), data->getContent().value() + data->getContent().value_size());

            if (deserializeModelData(oldbuffer, modelData) && vecIt != vec.end() && aggVecIt != aggVec.end()) {
                aggregate(modelData, seqNum);
                vec.erase(vecIt);
                aggVec.erase(aggVecIt);
            } else{
                NS_LOG_INFO("Data name doesn't exist in map_agg_oldSeq_newName, meaning this data packet is duplicate, do nothing!");
                return;
            }

            // Response time computation (RTT)
            if (currentTime.find(dataName) != currentTime.end()){
                responseTime[dataName] = ns3::Simulator::Now() - currentTime[dataName];
                ResponseTimeSum(responseTime[dataName].GetMilliSeconds());
                currentTime.erase(dataName);
                NS_LOG_INFO("Consumer's response time of sequence " << dataName << " is: " << responseTime[dataName].GetMilliSeconds() << " ms");
            }

            // Record response time store to a file
            responseTimeRecorder(responseTime[dataName]);

            // Set RTT_threshold to control cwnd
            if (RTT_threshold_vec.size() < numChild) {
                RTTThreshldMeasure(responseTime[dataName].GetMilliSeconds());
            }

            // Reset RetxTimer and timeout interval
            RTT_Timer = RTOMeasurement(responseTime[dataName].GetMilliSeconds());
            NS_LOG_DEBUG("responseTime for name : " << dataName << " is: " << responseTime[dataName].GetMilliSeconds() << " ms");
            NS_LOG_DEBUG("RTT measurement: " << RTT_Timer.GetMilliSeconds() << " ms");
            m_timeoutThreshold = RTT_Timer;



            // Check whether aggregation round finished
            if (vec.empty()) {
                NS_LOG_DEBUG("Aggregation for the round finished. ");
            }

            // Judge whether the aggregation iteration has finished
            if (aggVec.empty()) {
                NS_LOG_DEBUG("Aggregation of iteration " << seq << " has finished!");

                /// Perform actual aggregation for those data
                std::vector<float> aggregationResult = getMean(seqNum);


                // Calculate aggregate time
                if (aggregateStartTime.find(seq) != aggregateStartTime.end()) {
                    aggregateTime[seq] = ns3::Simulator::Now() - aggregateStartTime[seq];
                    AggregateTimeSum(aggregateTime[seq].GetMilliSeconds());
                    NS_LOG_DEBUG("Consumer's aggregation time is: " << aggregateTime[seq].GetMilliSeconds() << " for seq " << std::to_string(seq));
                    aggregateStartTime.erase(seq);
                } else {
                    NS_LOG_DEBUG("Error when calculating aggregation time, no reference found for seq " << seq);
                }

            }

            /// Stop simulation
            if (iteration == 100) {
                NS_LOG_DEBUG("Reach 100 iterations, stop!");
                ns3::Simulator::Stop();
                NS_LOG_INFO("The average aggregation time of Consumer in " << iteration << " iteration is: " << GetAggregateTimeAverage() << " ms");
                return;
            }
        } else {
            NS_LOG_DEBUG("Suspicious data packet, not exist in data map.");
        }


    } else if (type == "initialization") {
        std::string destNode = data->getName().get(0).toUri();
        NS_LOG_INFO("Node " << destNode << " has received aggregationTree map!");
    }
}



/**
 * Based on RTT of the first iteration, compute their RTT average as threshold, use the threshold for congestion control
 * @param responseTime
 */
void
Consumer::RTTThreshldMeasure(int64_t responseTime)
{
    RTT_threshold_vec.push_back(responseTime);
    if (RTT_threshold_vec.size() == numChild) {
        int64_t sum = 0;
        for (int64_t item: RTT_threshold_vec) {
            sum += item;
        }
        RTT_threshold = 2 * (sum / numChild);
        NS_LOG_INFO("RTT_threshold is set as: " << RTT_threshold << " ms");
    }
}



/**
 * Record the RTT every 5 ms, store them in a file
 */
void
Consumer::RTTRecorder()
{
    // Open the file using fstream in append mode
    std::ofstream file(RTT_recorder, std::ios::app);

    if (!file.is_open()) {
        std::cerr << "Failed to open the file: " << RTT_recorder << std::endl;
        return;
    }

    // Write the response_time to the file, followed by a newline
    file << ns3::Simulator::Now().GetMilliSeconds() << " " << RTT_Timer.GetMilliSeconds() << std::endl;

    // Close the file
    file.close();
    Simulator::Schedule(MilliSeconds(5), &Consumer::RTTRecorder, this);
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
