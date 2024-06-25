//
// Created by nathan on 18/06/24.
//

#include "schedulers/sOS/s_os_scheduler.h"

#include <memory>

#include "absl/strings/str_format.h"


void initResources(unsigned long resourceSize) {
    trace("TRACE: entering sOS::API::initResources\n");

    resourceList = (struct resource *) malloc(sizeof(struct resource) * resourceSize);

    trace("TRACE: exiting sOS::API::initResources\n");
}

static int last_index = 0;

void add_cpu(unsigned long id) {
    insertResource(&resourceList, id, last_index++);
}



namespace ghost {
    SOSScheduler::SOSScheduler(ghost::Enclave *enclave, ghost::CpuList cpulist,
                               std::shared_ptr <TaskAllocator<SOSTask>> allocator,
                               int32_t global_cpu,
                               absl::Duration preemption_time_slice)
    : BasicDispatchScheduler<SOSTask>(enclave, std::move(cpulist), std::move(allocator)),
    global_cpu_(global_cpu),
    global_channel_(GHOST_MAX_QUEUE_ELEMS, 0),
    preemption_time_slice_(preemption_time_slice) {
        trace("TRACE: entering ghost::scheduler::constructor\n");

        if (!cpus().IsSet(global_cpu_)) {
            Cpu c = cpus().Front();
            CHECK(c.valid());
            global_cpu_ = c.id();
        }
        initResources(MAX_CPUS);

        trace("TRACE: exiting ghost::scheduler::constructor\n");
    }

    SOSScheduler::~SOSScheduler() {}

    void SOSScheduler::EnclaveReady() {
        trace("TRACE: entering ghost::scheduler::EnclaveReady\n");

        for (const Cpu& cpu: cpus() ) {
            add_cpu((unsigned long)cpu.id());
            CpuState* cs = cpu_state(cpu);
            cs->agent = enclave()->GetAgent(cpu);
            CHECK_NE(cs->agent, nullptr);
        }

        policy->functions->init(MAX_CPUS);

        trace("TRACE: exiting ghost::scheduler::EnclaveReady\n");
    }

    bool SOSScheduler::Available(const ghost::Cpu &cpu) {
        CpuState * cs = cpu_state(cpu);

        if (cs->agent) return cs->agent->cpu_avail();

        return false;
    }

    int SOSScheduler::submitEvent(struct sOSEvent *event) {
        trace("TRACE: entering ghost::scheduler::submitEvent\n");

        const Cpu& cpu = topology()->cpu(event->physical_id);

        RunRequest* req = enclave()->GetRunRequest(cpu);
        printf("Submitting virtual %lu to physical %lu\n", event->virtual_id, event->physical_id);
        req->Open({.target = Gtid((int64_t) event->virtual_id),
                          .target_barrier = (BarrierToken) event->event_id,
                          // No need to set `agent_barrier` because the agent barrier is
                          // not checked when a global agent is scheduling a CPU other than
                          // the one that the global agent is currently running on.
                          .commit_flags = COMMIT_AT_TXN_COMMIT});

        /*enclave()->CommitRunRequests(found_cpu);
        req = enclave()->GetRunRequest(cpu);*/
        if (req->Commit()) {
            trace("TRACE: exiting ghost::scheduler::submitEvent\n");
            return 0;
        }
        else {
            printf("Req state %d\n", req->state()-INT_MIN);
            trace("TRACE: exiting ghost::scheduler::submitEvent -- error\n");
            return 1;
        }

    }

    bool SOSScheduler::PickNextGlobalCPU(ghost::BarrierToken agent_barrier, const ghost::Cpu &this_cpu) {
        // trace("TRACE: entering ghost::scheduler::PickNextGlobalCPU\n");

        Cpu target(Cpu::UninitializedType::kUninitialized);
        Cpu global_cpu = topology()->cpu(GetGlobalCPUId());
        int numa_node = global_cpu.numa_node();

        if (iterations_ & 0xff) {
            return false;
        }

        for (const Cpu& cpu: global_cpu.siblings()) {
            if (cpu.id() == global_cpu.id()) continue;

            if (Available(cpu)) {
                target = cpu;
                goto found;
            }
        }

        for (const Cpu& cpu : global_cpu.l3_siblings()) {
            if (cpu.id() == global_cpu.id()) continue;

            if (Available(cpu)) {
                target = cpu;
                goto found;
            }
        }

again:
        for (const Cpu& cpu : cpus()) {
            if (cpu.id() == global_cpu.id()) continue;

            if (numa_node >= 0 && cpu.numa_node() != numa_node) continue;

            if (Available(cpu)) {
                target = cpu;
                goto found;
            }
        }

        if (numa_node >= 0) {
            numa_node = -1;
            goto again;
        }

found:
        if (!target.valid()) return false;

        CHECK(target != this_cpu);

        CpuState* cs = cpu_state(target);
        SOSTask* prev = cs->current;
        if (prev) {
            CHECK(prev->oncpu());
        }

        printf("Set new global cpu %d\n", target.id());

        struct sOSEvent* event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

        event->physical_id = GetGlobalCPUId();
        event->sleep = AVAILABLE;

        policy->functions->on_sleep_state_change(event);

        event->physical_id = target.id();
        event->sleep = BUSY;

        policy->functions->on_sleep_state_change(event);

        free(event);

        SetGlobalCPU(target);
        enclave()->GetAgent(target)->Ping();

        return true;

        // trace("TRACE: exiting ghost::scheduler::PickNextGlobalCPU\n");
    }

    std::unique_ptr<SOSScheduler> SingleThreadSOSScheduler(
            Enclave* enclave, CpuList cpulist, int32_t global_cpu, absl::Duration preemption_time_slice
            ) {
        trace("TRACE: entering ghost::SingleThreadSOSScheduler\n");

        auto allocator = std::make_shared<SingleThreadMallocTaskAllocator<SOSTask>>();
        auto scheduler = std::make_unique<SOSScheduler>(
                enclave, std::move(cpulist), std::move(allocator), global_cpu, preemption_time_slice
                );

        trace("TRACE: exiting ghost::SingleThreadSOSScheduler\n");
        return scheduler;
    }

    void SOSAgent::AgentThread() {
        trace("TRACE: entering ghost::agent::AgentThread\n");

        Channel& global_channel = global_scheduler_->GetDefaultChannel();
        gtid().assign_name("Agent:"+std::to_string(cpu().id()));
        if (verbose() > 1) {
            printf("Agent tid:=%d\n", gtid().tid());
        }
        SignalReady();
        WaitForEnclaveReady();

        PeriodicEdge debug_out(absl::Seconds(1));

        while (!Finished() || !global_scheduler_->Empty()) {
            BarrierToken  agent_barrier = status_word().barrier();

            if (cpu().id() != global_scheduler_->GetGlobalCPUId()) {
                RunRequest* req = enclave()->GetRunRequest(cpu());

                req->LocalYield(agent_barrier, 0);
            } else {
                if (boosted_priority() && global_scheduler_->PickNextGlobalCPU(agent_barrier, cpu())) {
                    continue;
                }

                Message msg;
                while (!(msg = global_channel.Peek()).empty()) {
                    global_scheduler_->DispatchMessage(msg);
                    global_channel.Consume(msg);
                }

                global_scheduler_->GlobalSchedule(status_word(), agent_barrier);
            }
        }

        trace("TRACE: exiting ghost::agent::AgentThread\n");
    }

    void SOSScheduler::DumpState(const Cpu& agent_cpu, int flags) {}


    void SOSScheduler::TaskDiscovered(SOSTask* task) {
        trace("TRACE: entering ghost::scheduler::TaskDiscovered\n");
        trace("TRACE: exiting ghost::scheduler::TaskDiscovered\n");
    }
    void SOSScheduler::DiscoveryStart() {
        trace("TRACE: entering ghost::scheduler::DiscoveryStart\n");
        trace("TRACE: exiting ghost::scheduler::DiscoveryStart\n");
    }
    void SOSScheduler::DiscoveryComplete() {
        trace("TRACE: entering ghost::scheduler::DiscoveryComplete\n");
        trace("TRACE: exiting ghost::scheduler::DiscoveryComplete\n");
    }


    void SOSScheduler::TaskNew(ghost::SOSTask *task, const ghost::Message &msg) {
        trace("TRACE: entering ghost::scheduler::TaskNew\n");
        const ghost_msg_payload_task_new* payload =
                static_cast<const ghost_msg_payload_task_new*>(msg.payload());

        task->gtid = Gtid(payload->gtid);

        struct sOSEvent* event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

        event->attached_process = payload->parent_gtid;
        event->virtual_id = task->gtid.id();
        event->event_id = msg.seqnum();

        policy->functions->on_create_thread(event);

        if (payload->runnable) {
            policy->functions->on_ready(event);

            if (!policy->functions->select_phys_to_virtual(event)) {
                if (submitEvent(event)) {
                    policy->functions->on_yield(event);
                } else {
                    CpuState* cs = cpu_state(topology()->cpu(event->physical_id));
                    cs->used = true;
                }
            }
        }

        free(event);

        trace("TRACE: exiting ghost::scheduler::TaskNew\n");
    }

    void SOSScheduler::TaskRunnable(ghost::SOSTask *task, const ghost::Message &msg) {
        trace("TRACE: entering ghost::scheduler::TaskRunnable\n");

        struct sOSEvent* event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

        event->virtual_id = task->gtid.id();
        event->attached_process = task->gtid.id();
        event->event_id = msg.seqnum();

        policy->functions->on_ready(event);

        if (!policy->functions->select_phys_to_virtual(event)) {
            if (submitEvent(event)) {
                policy->functions->on_yield(event);
            } else {
                CpuState* cs = cpu_state(topology()->cpu(event->physical_id));
                cs->used = true;
            }
        }

        free(event);

        trace("TRACE: exiting ghost::scheduler::TaskRunnable\n");
    }

    void SOSScheduler::TaskDead(ghost::SOSTask *task, const ghost::Message &msg) {
        trace("TRACE: entering ghost::scheduler::TaskDead\n");

        const ghost_msg_payload_task_dead* payload =
                static_cast<const ghost_msg_payload_task_dead*>(msg.payload());

        struct sOSEvent* event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

        event->virtual_id = task->gtid.id();
        if (task->oncpu())
            event->physical_id = task->cpu.id();

        event->event_id = msg.seqnum();

        policy->functions->on_dead_thread(event);

        CpuState* cs = cpu_state(topology()->cpu(payload->cpu));
        cs->used = false;

        free(event);

        trace("TRACE: exiting ghost::scheduler::TaskDead\n");
    }

    void SOSScheduler::TaskDeparted(ghost::SOSTask *task, const ghost::Message &msg) {
        trace("TRACE: entering ghost::scheduler::TaskDeparted\n");

        const ghost_msg_payload_task_departed* payload =
                static_cast<const ghost_msg_payload_task_departed*>(msg.payload());

        struct sOSEvent* event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

        event->virtual_id = task->gtid.id();
        if (task->oncpu())
            event->physical_id = task->cpu.id();
        event->event_id = msg.seqnum();

        policy->functions->on_dead_thread(event);

        CpuState* cs = cpu_state(topology()->cpu(payload->cpu));
        cs->used = false;

        free(event);

        trace("TRACE: exiting ghost::scheduler::TaskDeparted\n");
    }

    void SOSScheduler::TaskYield(ghost::SOSTask *task, const ghost::Message &msg) {
        trace("TRACE: entering ghost::scheduler::TaskYield\n");

        const ghost_msg_payload_task_yield* payload =
                static_cast<const ghost_msg_payload_task_yield*>(msg.payload());

        struct sOSEvent* event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

        event->virtual_id = payload->gtid;
        if (task->oncpu())
            event->physical_id = payload->cpu;
        event->event_id = msg.seqnum();

        policy->functions->on_yield(event);

        CpuState* cs = cpu_state(topology()->cpu(payload->cpu));
        cs->used = false;

        free(event);

        trace("TRACE: exiting ghost::scheduler::TaskYield\n");
    }

    void SOSScheduler::TaskBlocked(ghost::SOSTask *task, const ghost::Message &msg) {
        trace("TRACE: entering ghost::scheduler::TaskBlocked\n");

        const ghost_msg_payload_task_blocked* payload =
                static_cast<const ghost_msg_payload_task_blocked*>(msg.payload());

        struct sOSEvent* event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

        event->virtual_id = payload->gtid;
        if (task->oncpu())
            event->physical_id = payload->cpu;
        event->event_id = msg.seqnum();

        policy->functions->on_invalid(event);

        CpuState* cs = cpu_state(topology()->cpu(payload->cpu));
        cs->used = false;

        free(event);

        trace("TRACE: exiting ghost::scheduler::TaskBlocked\n");
    }

    void SOSScheduler::TaskPreempted(ghost::SOSTask *task, const ghost::Message &msg) {
        trace("TRACE: entering ghost::scheduler::TaskPreempted\n");
        const ghost_msg_payload_task_preempt* payload =
                static_cast<const ghost_msg_payload_task_preempt*>(msg.payload());

        struct sOSEvent* event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

        event->virtual_id = payload->gtid;
        if (task->oncpu())
            event->physical_id = payload->cpu;
        event->event_id = msg.seqnum();

        policy->functions->on_yield(event);

        CpuState* cs = cpu_state(topology()->cpu(payload->cpu));
        cs->used = false;

        free(event);

        trace("TRACE: exiting ghost::scheduler::TaskPreempted\n");
    }

    void SOSScheduler::TaskSwitchto(ghost::SOSTask *task, const ghost::Message &msg) {
        trace("TRACE: entering ghost::scheduler::TaskSwitchto\n");

        struct sOSEvent* event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

        policy->functions->on_signal(event);

        free(event);

        trace("TRACE: exiting ghost::scheduler::TaskSwitchto\n");
    }

    void SOSScheduler::TaskAffinityChanged(ghost::SOSTask *task, const ghost::Message &msg) {
        trace("TRACE: entering ghost::scheduler::TaskAffinityChanged\n");

        struct sOSEvent* event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

        policy->functions->on_hints(event);

        free(event);

        trace("TRACE: exiting ghost::scheduler::TaskAffinityChanged\n");
    }

    void SOSScheduler::TaskPriorityChanged(ghost::SOSTask *task, const ghost::Message &msg) {
        trace("TRACE: entering ghost::scheduler::TaskPriorityChanged\n");

        struct sOSEvent* event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

        policy->functions->on_hints(event);

        free(event);

        trace("TRACE: exiting ghost::scheduler::TaskPriorityChanged\n");
    }

    void SOSScheduler::TaskOnCpu(ghost::SOSTask *task, const ghost::Message &msg) {
        trace("TRACE: entering ghost::scheduler::TaskOnCpu\n");
        const ghost_msg_payload_task_on_cpu* payload =
                static_cast<const ghost_msg_payload_task_on_cpu*>(msg.payload());

        struct sOSEvent* event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

        event->event_id = msg.seqnum();
        event->physical_id = payload->cpu;
        event->virtual_id = payload->gtid;

        policy->functions->restore_context(event);

        CpuState* cs = cpu_state(topology()->cpu(payload->cpu));
        cs->used = true;

        free(event);

        trace("TRACE: exiting ghost::scheduler::TaskOnCpu\n");
    }

    void SOSScheduler::CpuTick(const ghost::Message &msg) {
        trace("TRACE: entering ghost::scheduler::CpuTick\n");
        trace("TRACE: exiting ghost::scheduler::CpuTick\n");
    }

    void SOSScheduler::CpuNotIdle(const ghost::Message &msg) {
        trace("TRACE: entering ghost::scheduler::CpuNotIdle\n");
        trace("TRACE: exiting ghost::scheduler::CpuNotIdle\n");
    }

    void SOSScheduler::CpuTimerExpired(const ghost::Message &msg) {
        trace("TRACE: entering ghost::scheduler::CpuTimerExpired\n");
        trace("TRACE: exiting ghost::scheduler::CpuTimerExpired\n");
    }

    void SOSScheduler::CpuAvailable(const ghost::Message &msg) {
        trace("TRACE: entering ghost::scheduler::CpuAvailable\n");
        const ghost_msg_payload_cpu_available* payload =
                static_cast<const ghost_msg_payload_cpu_available*>(msg.payload());

        struct sOSEvent* event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

        event->event_id = msg.seqnum();
        event->physical_id = payload->cpu;
        event->sleep = AVAILABLE;

        policy->functions->on_sleep_state_change(event);

        free(event);

        trace("TRACE: exiting ghost::scheduler::CpuAvailable\n");
    }

    void SOSScheduler::CpuBusy(const ghost::Message &msg) {
        trace("TRACE: entering ghost::scheduler::CpuBusy\n");
        const ghost_msg_payload_cpu_busy* payload =
                static_cast<const ghost_msg_payload_cpu_busy*>(msg.payload());

        struct sOSEvent* event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

        event->event_id = msg.seqnum();
        event->physical_id = payload->cpu;
        event->sleep = BUSY;

        policy->functions->on_sleep_state_change(event);

        free(event);

        trace("TRACE: exiting ghost::scheduler::CpuBusy\n");
    }

    void SOSScheduler::AgentBlocked(const ghost::Message &msg) {
        trace("TRACE: entering ghost::scheduler::AgentBlocked\n");
        trace("TRACE: exiting ghost::scheduler::AgentBlocked\n");
    }

    void SOSScheduler::AgentWakeup(const ghost::Message &msg) {
        trace("TRACE: entering ghost::scheduler::AgentWakeup\n");
        trace("TRACE: exiting ghost::scheduler::AgentWakeup\n");
    }

    void SOSScheduler::GlobalSchedule(const StatusWord &agent_sw, BarrierToken agent_sw_last) {
        // trace("TRACE: entering ghost::scheduler::GlobalSchedule\n");
        const int global_cpu_id = GetGlobalCPUId();
        bool sched = false;

        for (const Cpu& cpu : cpus()) {
            CpuState* cs = cpu_state(cpu);

            if (!Available(cpu))
                continue;

            if (cpu.id() == global_cpu_id) {
                CHECK_EQ(cs->current, nullptr);
                continue;
            }

            struct sOSEvent* event = (struct sOSEvent*) malloc(sizeof(struct sOSEvent));

            event->physical_id = cpu.id();
            event->event_id = agent_sw_last;

            if(!policy->functions->select_virtual_to_load(event)) {
                printf("The global_cpu_id is %d while cpu id is %d\n", global_cpu_id, cpu.id());
                if (SOSScheduler::submitEvent(event)) {
                    policy->functions->on_yield(event);
                    cs->used = false;
                } else
                    cs->used = true;
                sched = true;
            }

            free(event);
        }

        if (sched)
            trace("TRACE: exiting ghost::scheduler::GlobalSchedule -- sched\n");
    }
}