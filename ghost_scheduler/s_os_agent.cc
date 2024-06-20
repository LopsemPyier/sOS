//
// Created by nathan on 18/06/24.
//

#include <cstdint>
#include <string>
#include <vector>

#include "absl/debugging/symbolize.h"
#include "absl/flags/parse.h"
#include "lib/agent.h"
#include "lib/channel.h"
#include "lib/enclave.h"
#include "lib/topology.h"
#include "schedulers/sOS/s_os_scheduler.h"

ABSL_FLAG(std::string, ghost_cpus, "1-5", "cpulist");
ABSL_FLAG(int32_t, globalcpu, -1,
"Global cpu. If -1, then defaults to the first cpu in <cpus>");
ABSL_FLAG(absl::Duration, preemption_time_slice, absl::InfiniteDuration(),
"A task is preempted after running for this time slice (default = "
"infinite time slice)");

namespace ghost {
    void ParseSOSConfig(SOSConfig* config) {
        CpuList ghost_cpus =
                MachineTopology()->ParseCpuStr(absl::GetFlag(FLAGS_ghost_cpus));
        // One CPU for the spinning global agent and at least one other for running
        // scheduled ghOSt tasks.
        CHECK_GE(ghost_cpus.Size(), 2);

        int globalcpu = absl::GetFlag(FLAGS_globalcpu);
        if (globalcpu < 0) {
            CHECK_EQ(globalcpu, -1);
            globalcpu = ghost_cpus.Front().id();
            absl::SetFlag(&FLAGS_globalcpu, globalcpu);
        }
        CHECK(ghost_cpus.IsSet(globalcpu));

        Topology* topology = MachineTopology();
        config->topology_ = topology;
        config->cpus_ = ghost_cpus;
        config->global_cpu_ = topology->cpu(globalcpu);
        config->preemption_time_slice_ = absl::GetFlag(FLAGS_preemption_time_slice);
    }

}

int main(int argc, char* argv[]) {

    absl::InitializeSymbolizer(argv[0]);

    absl::ParseCommandLine(argc, argv);

    ghost::SOSConfig config;
    ghost::ParseSOSConfig(&config);


    printf("Core map\n");

    int n = 0;
    for (const ghost::Cpu& c : config.topology_->all_cores()) {
        printf("( ");
        for (const ghost::Cpu& s : c.siblings()) printf("%2d ", s.id());
        printf(")%c", ++n % 8 == 0 ? '\n' : '\t');
    }
    printf("\n");

    printf("Initializing...\n");

    auto uap = new ghost::AgentProcess<ghost::FullSOSAgent<ghost::LocalEnclave>,
            ghost::SOSConfig>(config);

    ghost::GhostHelper()->InitCore();

    printf("Initialization complete, ghOSt active.\n");

    fflush(stdout);

    ghost::Notification exit;
    ghost::GhostSignals::AddHandler(SIGINT, [&exit](int) {
        static bool first = true;

        if (first) {
            exit.Notify();
            first = false;
            return false;
        }
        return true;
    });

    ghost::GhostSignals::AddHandler(SIGUSR1, [uap](int) {
        trace("TRACE: entering ghost::SIGUSR1_Handler\n");

        uap->Rpc(ghost::SOSScheduler::kDebugRunqueue);

        trace("TRACE: exiting ghost::SIGUSR1_Handler\n");
        return false;
    });

    exit.WaitForNotification();

    delete uap;
    printf("Done!\n");
    return 0;
}
