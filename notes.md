# Determine CPU topology

When running on a CPU with virtual cores (e.g. Intel/AMD CPUs with
hyperthreading, all virtual cores that share the same physical core should be
made exclusive together. To find out which virtual cores share the same
physical cores, look at
`/sys/devices/systems/cpu/cpu<N>/topology/core_cpus_list`

Note that this used to be called `thread_siblings_list`.

See also https://www.kernel.org/doc/html/v5.5/admin-guide/cputopology.html

# cpuset control

Apparently, what we must do is:

```
cd /sys/fs/cgroup/
echo +cpuset >cgroub.subtree_control
mkdir runexcl.slice
echo +cpuset >runexcl.slice/cgroup.subtree_control
echo 0-7 >runexcl.slice/cpuset.cpus
echo 0-7 >runexcl.slice/cpuset.cpus.exclusive
```

Now we can create so-called 'remote' cpuset partitions under runexcl.slice:
```
mkdir runexcl.slice/runexcl-1000.scope
echo 0 >runexcl.slice/runexcl-1000.scope/cpuset.cpus
echo root >runexcl.slice/runexcl-1000.scope/cpuset.cpus.partition
```

Things to keep in mind:
* `runexcl.slice/runexcl-1000.scope` is what the kernel documentation calls a
  'remote' cpuset partition, because it's parent cgroup (`runexcl.slice`) is not
  itself a cpuset partition.
* In `runexcl.slice`, both `cpuset.cpus` and `cpuset.cpus.exclusive` have to be
  set. They don't need to be identical, but probably should be.
* In `runexcl.slice/runexcl-1000.scope` it is sufficient to set `cpuset.cpus` to
  the cpus that should be used (exclusively) by this set.
* The `cpuset.cpus` and `cpuset.cpus.exclusive` files must all be set up
  correctly before writing `root` to `cpuset.cpus.partition`.

See https://docs.kernel.org/admin-guide/cgroup-v2.html and
    https://lwn.net/Articles/936555/ for more details.