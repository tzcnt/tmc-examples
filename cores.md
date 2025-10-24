To detect the number of CPU cores a container has been allocated from within the container, you can inspect the cgroup virtual file system, particularly if using cgroups v2.
Using /sys/fs/cgroup/cpu.max (for cgroups v2):
This file contains two values: cfs_quota_us and cfs_period_us. The cfs_quota_us represents the total CPU time in microseconds the container can use within a cfs_period_us (also in microseconds). The number of allocated cores can be derived by dividing cfs_quota_us by cfs_period_us.
Code

    cat /sys/fs/cgroup/cpu.max | awk '{print $1}' | tr ' ' '/' | bc
Note: If the container is unconstrained, /sys/fs/cgroup/cpu.max might contain "max" instead of a numerical quota, and the calculation will not yield a meaningful number of cores.
Using nproc.
The nproc command from the coreutils package can report the number of processing units available to the current process. This often reflects the CPU allocation within a container. 
Code

    nproc
Using /proc/self/status.
You can inspect the Cpus_allowed_list entry in /proc/self/status to see which CPU cores are available to the current process within the container. This provides a list or range of allowed CPU IDs.
Code

    grep Cpus_allowed_list /proc/self/status
Using taskset.
The taskset -c -p $$ command can display the CPU affinity of the current process, which can indicate the assigned cores.
Code

    taskset -c -p $$
These methods allow you to programmatically determine the CPU resources allocated to your container from within its environment.

IT WORKS if you use docker run --cpuset="0-3"
IT DOES NOT WORK if you use docker run --cpus="2"
