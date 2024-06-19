//
// Created by 李怿曈 on 16/4/2024.
//

#include "ndn-aggregator.hpp"
#include "model/ndn-l3-protocol.hpp"
#include "helper/ndn-fib-helper.hpp"
#include "ModelData.hpp"
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
#include <limits>

#include <set>
#include <map>
#include <vector>
#include <algorithm>
#include <string>
#include <numeric>
#include <iostream>
#include <sstream>

#include "utils/ndn-ns3-packet-tag.hpp"
#include "utils/ndn-rtt-mean-deviation.hpp"

#include <ndn-cxx/lp/tags.hpp>

#include <boost/lexical_cast.hpp>
#include <boost/ref.hpp>

NS_LOG_COMPONENT_DEFINE("ndn.Aggregator");

namespace ns3{
namespace ndn{

NS_OBJECT_ENSURE_REGISTERED(Aggregator);

TypeId
Aggregator::GetTypeId(void)
{
    static TypeId tid =
            TypeId("ns3::ndn::Aggregator")
            .SetGroupName("Ndn")
            .SetParent<App>()
            .AddConstructor<Aggregator>()
            .AddAttribute("StartSeq", "Starting sequence number", IntegerValue(0),
                          MakeIntegerAccessor(&Aggregator::m_seq), MakeIntegerChecker<int32_t>())
            .AddAttribute("Prefix", "Interest prefix/name", StringValue("/"),
                          MakeNameAccessor(&Aggregator::m_prefix), MakeNameChecker())
            .AddAttribute("LifeTime", "Life time for interest packet", StringValue("4s"),
                          MakeTimeAccessor(&Aggregator::m_interestLifeTime), MakeTimeChecker())
            .AddAttribute("RetxTimer",
                          "Timeout defining how frequent retransmission timeouts should be checked",
                          StringValue("50ms"),
                          MakeTimeAccessor(&Aggregator::GetRetxTimer, &Aggregator::SetRetxTimer),
                          MakeTimeChecker())
            .AddAttribute("Freshness", "Freshness of data packets, if 0, then unlimited freshness",
                          TimeValue(Seconds(0)), MakeTimeAccessor(&Aggregator::m_freshness),
                          MakeTimeChecker())
            .AddAttribute("Signature","Fake signature, 0 valid signature (default), other values application-specific",
                          UintegerValue(0), MakeUintegerAccessor(&Aggregator::m_signature),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("KeyLocator",
                          "Name to be used for key locator.  If root, then key locator is not used",
                          NameValue(), MakeNameAccessor(&Aggregator::m_keyLocator), MakeNameChecker())
            .AddAttribute("Window", "Initial size of the window", StringValue("1"),
                          MakeUintegerAccessor(&Aggregator::GetWindow, &Aggregator::SetWindow),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("MaxSeq", "Maximum sequence number to request (alternative to Size attribute, "
                                    "would activate only if Size is -1). "
                                    "The parameter is activated only if Size negative (not set)",
                            IntegerValue(std::numeric_limits<uint32_t>::max()),
                            MakeUintegerAccessor(&Aggregator::GetSeqMax, &Aggregator::SetSeqMax),
                            MakeUintegerChecker<uint32_t>())
            .AddAttribute("InitialWindowOnTimeout", "Set window to initial value when timeout occurs",
                          BooleanValue(true),
                          MakeBooleanAccessor(&Aggregator::m_setInitialWindowOnTimeout),
                          MakeBooleanChecker())
            .AddAttribute("Beta", "TCP Multiplicative Decrease factor", DoubleValue(0.5),
                          MakeDoubleAccessor(&Aggregator::m_beta),
                          MakeDoubleChecker<double>())
            .AddAttribute("AddRttSuppress", "Minimum number of RTTs (1 + this factor) between window decreases",
                          DoubleValue(0.5),
                          MakeDoubleAccessor(&Aggregator::m_addRttSuppress),
                          MakeDoubleChecker<double>())
            .AddAttribute("ReactToCongestionMarks",
                          "If true, process received congestion marks",
                          BooleanValue(true),
                          MakeBooleanAccessor(&Aggregator::m_reactToCongestionMarks),
                          MakeBooleanChecker())
            .AddAttribute("UseCwa",
                          "If true, use Conservative Window Adaptation",
                          BooleanValue(true),
                          MakeBooleanAccessor(&Aggregator::m_useCwa),
                          MakeBooleanChecker())
            .AddTraceSource("LastRetransmittedInterestDataDelay",
                            "Delay between last retransmitted Interest and received Data",
                            MakeTraceSourceAccessor(&Aggregator::m_lastRetransmittedInterestDataDelay),
                            "ns3::ndn::Aggregator::LastRetransmittedInterestDataDelayCallback")
            .AddTraceSource("FirstInterestDataDelay",
                            "Delay between first transmitted Interest and received Data",
                            MakeTraceSourceAccessor(&Aggregator::m_firstInterestDataDelay),
                            "ns3::ndn::Aggregator::FirstInterestDataDelayCallback")
            .AddTraceSource("WindowTrace",
                            "Window that controls how many outstanding interests are allowed",
                            MakeTraceSourceAccessor(&Aggregator::m_window),
                            "ns3::ndn::Aggregator::WindowTraceCallback")
            .AddTraceSource("InFlight", "Current number of outstanding interests",
                            MakeTraceSourceAccessor(&Aggregator::m_inFlight),
                            "ns3::ndn::Aggregator::WindowTraceCallback");
    return tid;

}

Aggregator::Aggregator()
    : m_rand(CreateObject<UniformRandomVariable>())
    , m_inFlight(0)
    , m_ssthresh(std::numeric_limits<double>::max())
    , m_highData(0)
    , m_recPoint(0.0)
    , m_seq(0)
    , SmoothedRTT(0)
    , roundRTT(0)
    , totalResponseTime(0)
    , round(0)
    , totalAggregateTime(0)
    , iteration(0)
{
    m_rtt = CreateObject<RttMeanDeviation>();
}




// Helper function to split string by a delimiter and return vector of strings
std::pair<std::string, std::set<std::string>>
Aggregator::aggTreeProcessSingleString(const std::string& input)
{
    std::istringstream iss(input);
    std::string segment;
    std::vector<std::string> segments;

    // Use getline to split the string by '.'
    while (getline(iss, segment, '.')) {
        segments.push_back(segment);
    }

    // Check if there are enough segments to form a key and a set
    if (segments.size() > 1) {
        std::string key = segments[0];
        std::set<std::string> values(segments.begin() + 1, segments.end());
        return {key, values};
    }

    return {};  // Return an empty pair if not enough segments
}


std::map<std::string, std::set<std::string>>
Aggregator::aggTreeProcessStrings(const std::vector<std::string>& inputs)
{
    std::map<std::string, std::set<std::string>> result;

    for (const std::string& input : inputs) {
        auto entry = aggTreeProcessSingleString(input);
        if (!entry.first.empty()) {
            result[entry.first].insert(entry.second.begin(), entry.second.end());
        }
    }

    return result;
}


void
Aggregator::ResponseTimeSum (int64_t response_time)
{
    totalResponseTime += response_time;
    ++round;
}

int64_t
Aggregator::GetResponseTimeAverage()
{
    if (round == 0)
    {
        NS_LOG_DEBUG("Error happened when calculating average response time!");
        return 0;
    }

    return totalResponseTime / round;
}


void
Aggregator::AggregateTimeSum (int64_t aggregate_time)
{
    totalAggregateTime += aggregate_time;
    ++iteration;
}

int64_t
Aggregator::GetAggregateTimeAverage()
{
    if (iteration == 0)
    {
        NS_LOG_DEBUG("Error happened when calculating aggregate time!");
        return 0;
    }

    return totalAggregateTime / iteration;
}


void
Aggregator::CheckRetxTimeout()
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
    m_retxEvent = Simulator::Schedule(m_retxTimer, &Aggregator::CheckRetxTimeout, this);
}


Time
Aggregator::RTTMeasurement(int64_t resTime)
{
    if (roundRTT == 0) {
        SmoothedRTT += resTime;
    } else {
        SmoothedRTT = SmoothedRTT * 0.875 + resTime * 0.125;
    }
    roundRTT++;

    if (roundRTT < 5) {
        NS_LOG_DEBUG("Round is less than 5, use original threshold: " << (m_retxTimer*6).GetMilliSeconds());
        return m_retxTimer * 6;
    } else {
        NS_LOG_DEBUG("New timeout interval: " << static_cast<int64_t>(SmoothedRTT * 4) << " ms");
        //return std::max(m_retxTimer * 6, MilliSeconds(SmoothedRTT * 2));
        return m_retxTimer * 6;
    }
}



void
Aggregator::OnTimeout(std::string nameString)
{
    /// Designed for AIMD
    WindowDecrease();

    if (m_inFlight > static_cast<uint32_t>(0)){
        m_inFlight--;
    }
    NS_LOG_DEBUG("Window: " << m_window << ", InFlight: " << m_inFlight);

    shared_ptr<Name> name = make_shared<Name>(nameString);
    SendInterest(name);

}


void
Aggregator::SetRetxTimer(Time retxTimer)
{
    m_retxTimer = retxTimer;
    if (m_retxEvent.IsRunning()) {
        Simulator::Remove(m_retxEvent);
    }

    // Schedule new timeout
    m_timeoutThreshold = retxTimer;
    //NS_LOG_DEBUG("Next interval to check timeout is: " << m_retxTimer.GetMilliSeconds() << " ms");
    m_retxEvent = Simulator::Schedule(m_retxTimer, &Aggregator::CheckRetxTimeout, this);
}

Time
Aggregator::GetRetxTimer() const
{
    return m_retxTimer;
}

void
Aggregator::StartApplication()
{
    NS_LOG_FUNCTION_NOARGS();
    App::StartApplication();
    FibHelper::AddRoute(GetNode(), m_prefix, m_face, 0);
    //Simulator::Schedule(Seconds(0.05), &Aggregator::CheckRetxTimeout, this);
}

void
Aggregator::StopApplication()
{
    /// Cancel packet generation - can be a way to stop simulation gracefully?
    Simulator::Cancel(m_sendEvent);

    //NS_LOG_INFO("The average response time of Aggregator in " << round << " aggregation rounds is: " << GetResponseTimeAverage() << " ms");
    //NS_LOG_INFO("The average aggregate time is: " << GetAggregateTimeAverage() << " ms");
    App::StopApplication();
}

void Aggregator::aggregate(const ModelData& data, const std::string& dataName) {
    // first initialization
    if (sumParameters.find(dataName) == sumParameters.end()){
        sumParameters[dataName] = std::vector<float>(300, 0.0f);
        count[dataName] = 0;
    }

    // Aggregate data
    std::transform(sumParameters[dataName].begin(), sumParameters[dataName].end(), data.parameters.begin(), sumParameters[dataName].begin(), std::plus<float>());
    count[dataName]++;
}

ModelData Aggregator::getMean(const std::string& dataName){
    ModelData result;
    if (sumParameters.find(dataName) != sumParameters.end()) {
        result.parameters = sumParameters[dataName];  // Direct assignment
    }

    return result;
}

void
Aggregator::OnNack(shared_ptr<const lp::Nack> nack)
{
    /// tracing inside
    App::OnNack(nack);

    NS_LOG_INFO("NACK received for: " << nack->getInterest().getName()
                                          << ", reason: " << nack->getReason());
}


void
Aggregator::SetWindow(uint32_t window)
{
    m_initialWindow = window;
    m_window = m_initialWindow;
}

uint32_t
Aggregator::GetWindow() const
{
    return m_initialWindow;
}

void
Aggregator::SetSeqMax(uint32_t seqMax)
{
    // Be careful, ignore maxSize here
    m_seqMax = seqMax;
}

uint32_t
Aggregator::GetSeqMax() const
{
    return m_seqMax;
}


void
Aggregator::WindowIncrease()
{
    if (m_window < m_ssthresh) {
        m_window += 1.0;
    }else {
        m_window += (1.0 / m_window);
    }
    NS_LOG_DEBUG("Window size increased to " << m_window);
}

void
Aggregator::WindowDecrease()
{
    if (!m_useCwa || m_highData > m_recPoint) {
        const double diff = m_seq - m_highData;
        BOOST_ASSERT(diff > 0);

        m_recPoint = m_seq + (m_addRttSuppress * diff);

        // AIMD
        m_ssthresh = m_window * m_beta;
        m_window = m_ssthresh;

        // Window size can't be reduced below initial size
        if (m_window < m_initialWindow) {
            m_window = m_initialWindow;
        }

        NS_LOG_DEBUG("Window size decreased to " << m_window);
    }
    else {
        NS_LOG_DEBUG("Window decrease suppressed, HighData: " << m_highData << ", RecPoint: " << m_recPoint);
    }

}


void
Aggregator::OnInterest(shared_ptr<const Interest> interest)
{
    NS_LOG_INFO("Receiving interest:  " << *interest);
    NS_LOG_INFO("The incoming interest packet size is: " << interest->wireEncode().size());
    App::OnInterest(interest);

    std::string interestType = interest->getName().get(-2).toUri();

    NS_LOG_INFO("interestType: " << interestType);
    if (interestType == "data") {

        // Parse incoming interest, retrieve their name segments, currently use "/NextHop/Destination/Type/SeqNum"
        std::string dest = interest->getName().get(1).toUri();
        uint32_t seq = interest->getName().get(-1).toSequenceNumber();
        std::string originalName = interest->getName().toUri();

        std::vector<std::string> segments;  // Vector to store the segments
        std::istringstream iss(dest);
        std::string segment;

        // design for data aggregation, store the info about how interest is divided into segments
        std::vector<std::string> value_agg;

        // split the destination segment into several ones and store them individually in a vector called "segments"
        while (std::getline(iss, segment, '.')) {
            segments.push_back(segment);
        }

        // Start Compute aggregation time
        if (aggregateStartTime.find(seq) == aggregateStartTime.end())
            aggregateStartTime[seq] = ns3::Simulator::Now();

        // iterate all child nodes in a loop, assign the "name" based on child node's prefix
        for (const auto& [child, leaves] : aggregationMap) {
            std::string name_sec1;
            std::string name;

            // interest is divided
            for (const auto& leaf : leaves) {
                if (std::find(segments.begin(), segments.end(), leaf) != segments.end()) {
                    name_sec1 += leaf + ".";
                } else {
                    NS_LOG_INFO("Data from " << leaf << " is not required for this iteration.");
                }
            }
            name_sec1.resize(name_sec1.size() - 1);

            if (name_sec1.empty()) {
                NS_LOG_INFO("No interest needs to be sent to node " << child << " in this iteration");
                continue;
            } else {
                name = "/" + child + "/" + name_sec1 + "/data";
                shared_ptr<Name> newName = make_shared<Name>(name);
                newName->appendSequenceNumber(seq);
                value_agg.push_back(newName->toUri());

                /// Store relevant into FIFO queue, schedule the send event later
                interestQueue.push(newName);
            }
        }

        if (map_agg_oldSeq_newName.find(seq) == map_agg_oldSeq_newName.end() && m_agg_newDataName.find(seq) == m_agg_newDataName.end()){
            map_agg_oldSeq_newName[seq] = value_agg;
            m_agg_newDataName[seq] = originalName;
        }
        ScheduleNextPacket();

    } else if (interestType == "initialization") {
        // Extract useful info and parse it into readable format
        std::vector<std::string> inputs;
        if (interest->getName().size() > 3) {
            for (size_t i = 1; i < interest->getName().size() - 2; ++i) {
                inputs.push_back(interest->getName().get(i).toUri());
            }
        }
        aggregationMap = aggTreeProcessStrings(inputs);

        // testing, delete later!!!!
        if (aggregationMap.empty())
            NS_LOG_DEBUG("aggregationMap is empty!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        else {
            // Print the result mapping
            for (const auto& [node, leaves] : aggregationMap) {
                NS_LOG_DEBUG(node << " contains leaf nodes: ");
                for (const auto& leaf : leaves) {
                    NS_LOG_DEBUG(leaf << " ");
                }
            }
        }

        Name dataName(interest->getName());
        auto data = make_shared<Data>();
        data->setName(dataName);
        data->setFreshnessPeriod(::ndn::time::milliseconds(m_freshness.GetMilliSeconds()));

        SignatureInfo signatureInfo(static_cast< ::ndn::tlv::SignatureTypeValue>(255));
        if (m_keyLocator.size() > 0) {
            signatureInfo.setKeyLocator(m_keyLocator);
        }
        data->setSignatureInfo(signatureInfo);
        ::ndn::EncodingEstimator estimator;
        ::ndn::EncodingBuffer encoder(estimator.appendVarNumber(m_signature), 0);
        encoder.appendVarNumber(m_signature);
        data->setSignatureValue(encoder.getBuffer());

        // to create real wire encoding
        data->wireEncode();
        m_transmittedDatas(data, this, m_face);
        m_appLink->onReceiveData(*data);
    }
}

void
Aggregator::ScheduleNextPacket()
{
    if (m_window == static_cast<uint32_t>(0)) {
        Simulator::Remove(m_sendEvent);
        NS_LOG_DEBUG("New event in " << (std::min<double>(0.5, m_rtt->RetransmitTimeout().ToDouble(Time::S))) << " sec");

        m_sendEvent = Simulator::Schedule(Seconds(std::min<double>(0.5, (m_retxTimer * 6).GetSeconds())), &Aggregator::SendPacket, this);
    }
    else if (m_inFlight >= m_window) {
        // do nothing
        NS_LOG_INFO("m_inFlight >= m_window, do nothing.");
    }
    else {
        // This step seems necessary if "m_window = 0" condition is triggered before
        if (m_sendEvent.IsRunning()) {
            Simulator::Remove(m_sendEvent);
        }
        NS_LOG_DEBUG("Window: " << m_window << ", InFlight: " << m_inFlight);
        m_sendEvent = Simulator::ScheduleNow(&Aggregator::SendPacket, this);
    }
}


void
Aggregator::SendPacket()
{
    if (!interestQueue.empty()){
        auto interestInfo = interestQueue.front();
        interestQueue.pop();
        shared_ptr<Name> newName = interestInfo;
        SendInterest(newName);

        ScheduleNextPacket();
    } else {
        NS_LOG_INFO("No info in the queue, aggregator can't send anything!");
    }
}

void
Aggregator::SendInterest(shared_ptr<Name> newName)
{
    if (!m_active)
        return;

    std::string nameWithSeq = newName->toUri();

    // Trace timeout
    m_timeoutCheck[nameWithSeq] = ns3::Simulator::Now();

    // Start response time
    currentTime[nameWithSeq] = ns3::Simulator::Now();

    NS_LOG_INFO("Sending new interest >>>> " << nameWithSeq);
    shared_ptr<Interest> newInterest = make_shared<Interest>();
    newInterest->setNonce(m_rand->GetValue(0, std::numeric_limits<uint32_t>::max()));
    newInterest->setCanBePrefix(false);
    newInterest->setName(*newName);
    time::milliseconds interestLifeTime(m_interestLifeTime.GetMilliSeconds());
    newInterest->setInterestLifetime(interestLifeTime);
    m_transmittedInterests(newInterest, this, m_face);
    m_appLink->onReceiveInterest(*newInterest);

    /// Designed for Window
    m_inFlight++;
}

void
Aggregator::OnData(shared_ptr<const Data> data)
{
    if(!m_active)
        return;

    App::OnData(data);
    NS_LOG_INFO ("Received content object: " << boost::cref(*data));
    NS_LOG_INFO("The incoming data packet size is: " << data->wireEncode().size());

    std::string dataName = data->getName().toUri();
    std::string seqNum = data->getName().get(-1).toUri();
    uint32_t seq = data->getName().at(-1).toSequenceNumber();

    // Stop checking timeout associated with this seq
    if (m_timeoutCheck.find(dataName) == m_timeoutCheck.end())
        m_timeoutCheck.erase(dataName);
    else
        NS_LOG_DEBUG("Data " << dataName << " doesn't exist in the map, please check!");

    /// Designed for AIMD
    if (m_highData < seq) {
        m_highData = seq;
    }

    if (data->getCongestionMark() > 0) {
        if (m_reactToCongestionMarks) {
            NS_LOG_DEBUG("Received congestion mark: " << data->getCongestionMark());
            WindowDecrease();
        }
        else {
            NS_LOG_DEBUG("Ignored received congestion mark: " << data->getCongestionMark());
        }
    } else {
        WindowIncrease();
    }

    if (m_inFlight > static_cast<uint32_t>(0)) {
        m_inFlight--;
    }

    NS_LOG_DEBUG("Window: " << m_window << ", InFlight: " << m_inFlight);

    ScheduleNextPacket();


    // Check what are the exact names of data waiting for aggregation
    ModelData modelData;
    auto data_map = map_agg_oldSeq_newName.find(seq);
    if (data_map != map_agg_oldSeq_newName.end())
    {
        NS_LOG_INFO("Received data name: " << data->getName().toUri());

        auto& vec = data_map->second;
        auto vecIt = std::find(vec.begin(), vec.end(), dataName);
        std::vector<uint8_t> oldbuffer(data->getContent().value(), data->getContent().value() + data->getContent().value_size());

        if (deserializeModelData(oldbuffer, modelData) && vecIt != vec.end()) {
            aggregate(modelData, seqNum);
            vec.erase(vecIt);
        } else{
            NS_LOG_INFO("Data name doesn't exist in map_agg_oldSeq_newName, meaning this data packet is duplicate, do nothing!");
            return;
        }

        // Response time computation (RTT)
        if (currentTime.find(dataName) != currentTime.end()){
            responseTime[dataName] = ns3::Simulator::Now() - currentTime[dataName];
            ResponseTimeSum(responseTime[dataName].GetMilliSeconds());
            currentTime.erase(dataName);
        }

        // Reset RetxTimer and timeout interval
        RTT_Timer = RTTMeasurement(responseTime[dataName].GetMilliSeconds());
        m_timeoutThreshold = RTT_Timer;
        NS_LOG_DEBUG("responseTime for name : " << dataName << " is: " << responseTime[dataName].GetMilliSeconds() << " ms");
        NS_LOG_DEBUG("RTT measurement: " << RTT_Timer.GetMilliSeconds() << " ms");


        // Check the mapping to judge whether the aggregation process is done
        if (vec.empty()){
            NS_LOG_DEBUG("Aggregation finished.");
            // reset the aggregation count
            count[seqNum] = 0;

            // Aggregation time computation
            if (aggregateStartTime.find(seq) != aggregateStartTime.end()) {
                aggregateTime[seq] = ns3::Simulator::Now() - aggregateStartTime[seq];
                AggregateTimeSum(aggregateTime[seq].GetMilliSeconds());
                aggregateStartTime.erase(seq);
                NS_LOG_INFO("Aggregator's aggregate time of sequence " << seq << " is: " << aggregateTime[seq].GetMilliSeconds() << " ms");
            } else {
                NS_LOG_DEBUG("Error when calculating aggregation time, no reference found for seq " << seq);
            }

            // Add them together, perform average at consumer!!!!
            std::vector<uint8_t> newbuffer;
            serializeModelData(getMean(seqNum), newbuffer);

            // create data packet
            auto data = make_shared<Data>();

            std::string name_string = m_agg_newDataName[seq];
            NS_LOG_DEBUG("New aggregated data's name: " << name_string);

            shared_ptr<Name> newName = make_shared<Name>(name_string);
            data->setName(*newName);
            data->setContent(make_shared< ::ndn::Buffer>(newbuffer.begin(), newbuffer.end()));
            data->setFreshnessPeriod(::ndn::time::milliseconds(m_freshness.GetMilliSeconds()));
            SignatureInfo signatureInfo(static_cast< ::ndn::tlv::SignatureTypeValue>(255));

            if (m_keyLocator.size() > 0){
                signatureInfo.setKeyLocator(m_keyLocator);
            }
            data->setSignatureInfo(signatureInfo);
            ::ndn::EncodingEstimator estimator;
            ::ndn::EncodingBuffer encoder(estimator.appendVarNumber(m_signature), 0);
            encoder.appendVarNumber(m_signature);
            data->setSignatureValue(encoder.getBuffer());
            data->wireEncode();
            m_transmittedDatas(data, this, m_face);
            m_appLink->onReceiveData(*data);
        } else{
            NS_LOG_DEBUG("Wait for others to aggregate.");
        }
    }else{
        NS_LOG_DEBUG("Error, data name can't be recognized!");
    }
}

} // namespace ndn
} // namespace ns3


