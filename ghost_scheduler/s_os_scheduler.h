//
// Created by nathan on 18/06/24.
//

#ifndef GHOST_USERSPACE_S_OS_SCHEDULER_H
#define GHOST_USERSPACE_S_OS_SCHEDULER_H

#include <cstdint>
#include <map>
#include <memory>

#include "absl/time/time.h"
#include "lib/agent.h"
#include "lib/scheduler.h"
#include "policies/ghost_linking.h"

namespace ghost {

    struct SOSTask : public Task<> {
        enum class RunState {
            kBlocked,
            kQueued,
            kRunnable,
            kOnCpu,
            kYielding,
        };

        SOSTask(Gtid fifo_task_gtid, ghost_sw_info sw_info)
        : Task<>(fifo_task_gtid, sw_info) {}
        ~SOSTask() override {}

        bool blocked() const { return run_state == RunState::kBlocked; }
        bool queued() const { return run_state == RunState::kQueued; }
        bool runnable() const { return run_state == RunState::kRunnable; }
        bool oncpu() const { return run_state == RunState::kOnCpu; }
        bool yielding() const { return run_state == RunState::kYielding; }

        static std::string_view RunStateToString(SOSTask::RunState run_state) {
            switch (run_state) {
                case SOSTask::RunState::kBlocked:
                    return "Blocked";
                case SOSTask::RunState::kQueued:
                    return "Queued";
                case SOSTask::RunState::kRunnable:
                    return "Runnable";
                case SOSTask::RunState::kOnCpu:
                    return "OnCpu";
                case SOSTask::RunState::kYielding:
                    return "Yielding";
            }
        }

        friend std::ostream& operator<<(std::ostream& os,
                                        SOSTask::RunState run_state) {
            return os << RunStateToString(run_state);
        }

        RunState run_state = RunState::kBlocked;
        Cpu cpu{Cpu::UninitializedType::kUninitialized};
    };

    class SOSScheduler : public BasicDispatchScheduler<SOSTask> {
    public:
        SOSScheduler(Enclave* enclave, CpuList cpulist, std::shared_ptr<TaskAllocator<SOSTask>> allocator, int32_t global_cpu, absl::Duration preemption_time_slice);
        ~SOSScheduler();

        void EnclaveReady();
        Channel& GetDefaultChannel() { return global_channel_; };

        void TaskNew(SOSTask* task, const Message& msg);
        void TaskRunnable(SOSTask* task, const Message& msg);
        void TaskDead(SOSTask* task, const Message& msg);
        void TaskDeparted(SOSTask* task, const Message& msg);
        void TaskYield(SOSTask* task, const Message& msg);
        void TaskBlocked(SOSTask* task, const Message& msg);
         void TaskPreempted(SOSTask* task, const Message& msg);
        void TaskSwitchto(SOSTask* task, const Message& msg);
        void TaskAffinityChanged(SOSTask* task, const Message& msg);
        void TaskPriorityChanged(SOSTask* task, const Message& msg);
        void TaskOnCpu(SOSTask* task, const Message& msg);

        void TaskDiscovered(SOSTask* task);
        void DiscoveryStart();
        void DiscoveryComplete();

        void CpuTick(const Message& msg);
        void CpuNotIdle(const Message& msg);
        void CpuTimerExpired(const Message& msg);
        void CpuAvailable(const Message& msg);
        void CpuBusy(const Message& msg);
        void AgentBlocked(const Message& msg);
        void AgentWakeup(const Message& msg);

        int submitEvent(struct sOSEvent* event);

        bool Empty() { return num_tasks_ == 0; }

        void GlobalSchedule(const StatusWord& agent_sw, BarrierToken agent_sw_last);

        int32_t GetGlobalCPUId() {
            return global_cpu_.load(std::memory_order_acquire);
        }

        void SetGlobalCPU(const Cpu& cpu) {
            global_cpu_core_ = cpu.core();
            global_cpu_.store(cpu.id(), std::memory_order_release);
        }

        bool PickNextGlobalCPU(BarrierToken agent_barrier, const Cpu& this_cpu);

        void DumpState(const Cpu& cpu, int flags);
        std::atomic<bool> debug_runqueue_ = false;

        static const int kDebugRunqueue = 1;

    private:
        struct policy_detail* policy = &rr_policy_detail;

        struct CpuState {
            SOSTask * current = nullptr;
            bool used = false;
            const Agent* agent = nullptr;
        } ABSL_CACHELINE_ALIGNED;

        bool Available(const Cpu& cpu);

        CpuState* cpu_state(const Cpu& cpu) {return &cpu_states_[cpu.id()];}

        int global_cpu_core_;
        std::atomic<int32_t> global_cpu_;
        LocalChannel global_channel_;
        int num_tasks_ = 0;

        CpuState cpu_states_[MAX_CPUS];
        const absl::Duration preemption_time_slice_;


        uint64_t iterations_ = 0;
    };

    std::unique_ptr<SOSScheduler> SingleThreadSOSScheduler(
            Enclave* enclave, CpuList cpulist, int32_t global_cpu,
            absl::Duration preemption_time_slice);

    class SOSAgent : public LocalAgent {
    public:
        SOSAgent(Enclave* enclave, Cpu cpu, SOSScheduler* global_scheduler)
        : LocalAgent(enclave, cpu), global_scheduler_(global_scheduler) {}

        void AgentThread() override;
        Scheduler* AgentScheduler() const override { return global_scheduler_; }

    private:
        SOSScheduler* global_scheduler_;
    };

    class SOSConfig : public AgentConfig {
    public:
        SOSConfig() {}
        SOSConfig(Topology* topology, CpuList cpulist, Cpu global_cpu,
                   absl::Duration preemption_time_slice)
                : AgentConfig(topology, std::move(cpulist)),
                  global_cpu_(global_cpu),
                  preemption_time_slice_(preemption_time_slice) {}

        Cpu global_cpu_{Cpu::UninitializedType::kUninitialized};
        absl::Duration preemption_time_slice_ = absl::InfiniteDuration();
    };

    template <class EnclaveType>
    class FullSOSAgent : public FullAgent<EnclaveType> {
    public:
        explicit FullSOSAgent(SOSConfig config) : FullAgent<EnclaveType>(config) {
            global_scheduler_ = SingleThreadSOSScheduler(
                    &this->enclave_, *this->enclave_.cpus(), config.global_cpu_.id(),
                    config.preemption_time_slice_);
            this->StartAgentTasks();
            this->enclave_.Ready();
        }

        ~FullSOSAgent() override {
            // Terminate global agent before satellites to avoid a false negative error
            // from ghost_run(). e.g. when the global agent tries to schedule on a CPU
            // without an active satellite agent.
            auto global_cpuid = global_scheduler_->GetGlobalCPUId();

            if (this->agents_.front()->cpu().id() != global_cpuid) {
                // Bring the current globalcpu agent to the front.
                for (auto it = this->agents_.begin(); it != this->agents_.end(); it++) {
                    if (((*it)->cpu().id() == global_cpuid)) {
                        auto d = std::distance(this->agents_.begin(), it);
                        std::iter_swap(this->agents_.begin(), this->agents_.begin() + d);
                        break;
                    }
                }
            }

            CHECK_EQ(this->agents_.front()->cpu().id(), global_cpuid);

            this->TerminateAgentTasks();
        }

        std::unique_ptr<Agent> MakeAgent(const Cpu& cpu) override {
            return std::make_unique<SOSAgent>(&this->enclave_, cpu,
                                               global_scheduler_.get());
        }

        void RpcHandler(int64_t req, const AgentRpcArgs& args,
                        AgentRpcResponse& response) override {
            switch (req) {
                case SOSScheduler::kDebugRunqueue:
                    global_scheduler_->debug_runqueue_ = true;
                    response.response_code = 0;
                    return;
                default:
                    response.response_code = -1;
                    return;
            }
        }

    private:
        std::unique_ptr<SOSScheduler> global_scheduler_;
    };
}

#endif //GHOST_USERSPACE_S_OS_SCHEDULER_H
