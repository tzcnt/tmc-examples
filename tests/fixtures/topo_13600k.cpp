#include <string>

#include "fixtures.hpp"

const std::string topo_13600k =
  R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE topology SYSTEM "hwloc2.dtd">
<topology version="2.0">
  <object type="Machine" os_index="0" cpuset="0x000fffff" complete_cpuset="0x000fffff" allowed_cpuset="0x000fffff" nodeset="0x00000001" complete_nodeset="0x00000001" allowed_nodeset="0x00000001" gp_index="1">
    <info name="DMIProductName" value="System Product Name"/>
    <info name="DMIProductVersion" value="System Version"/>
    <info name="DMIBoardVendor" value="ASUSTeK COMPUTER INC."/>
    <info name="DMIBoardName" value="ROG STRIX B760-I GAMING WIFI"/>
    <info name="DMIBoardVersion" value="Rev 1.xx"/>
    <info name="DMIBoardAssetTag" value="Default string"/>
    <info name="DMIChassisVendor" value="Default string"/>
    <info name="DMIChassisType" value="3"/>
    <info name="DMIChassisVersion" value="Default string"/>
    <info name="DMIChassisAssetTag" value="Default string"/>
    <info name="DMIBIOSVendor" value="American Megatrends Inc."/>
    <info name="DMIBIOSVersion" value="1825"/>
    <info name="DMIBIOSDate" value="10/09/2025"/>
    <info name="DMISysVendor" value="ASUS"/>
    <info name="Backend" value="Linux"/>
    <info name="LinuxCgroup" value="/user.slice/user-1000.slice/user@1000.service/app.slice/ptyxis-spawn-d2ab9d6e-d87c-4383-8b3d-6d45a3050d9a.scope"/>
    <info name="OSName" value="Linux"/>
    <info name="OSRelease" value="6.17.0-8-generic"/>
    <info name="OSVersion" value="#8-Ubuntu SMP PREEMPT_DYNAMIC Fri Nov 14 21:44:46 UTC 2025"/>
    <info name="HostName" value="coffee"/>
    <info name="Architecture" value="x86_64"/>
    <info name="hwlocVersion" value="2.12.2"/>
    <info name="ProcessName" value="lstopo"/>
    <object type="Package" os_index="0" cpuset="0x000fffff" complete_cpuset="0x000fffff" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="3">
      <info name="CPUVendor" value="GenuineIntel"/>
      <info name="CPUFamilyNumber" value="6"/>
      <info name="CPUModelNumber" value="183"/>
      <info name="CPUModel" value="13th Gen Intel(R) Core(TM) i5-13600KF"/>
      <info name="CPUStepping" value="1"/>
      <object type="NUMANode" os_index="0" cpuset="0x000fffff" complete_cpuset="0x000fffff" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="83" local_memory="32857645056">
        <page_type size="4096" count="8021886"/>
        <page_type size="2097152" count="0"/>
        <page_type size="1073741824" count="0"/>
      </object>
      <object type="L3Cache" os_index="0" cpuset="0x000fffff" complete_cpuset="0x000fffff" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="10" cache_size="25165824" depth="3" cache_linesize="64" cache_associativity="12" cache_type="0">
        <info name="Inclusive" value="0"/>
        <object type="L2Cache" os_index="0" cpuset="0x00000003" complete_cpuset="0x00000003" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="9" cache_size="2097152" depth="2" cache_linesize="64" cache_associativity="16" cache_type="0">
          <info name="Inclusive" value="0"/>
          <object type="L1Cache" os_index="0" cpuset="0x00000003" complete_cpuset="0x00000003" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="7" cache_size="49152" depth="1" cache_linesize="64" cache_associativity="12" cache_type="1">
            <info name="Inclusive" value="0"/>
            <object type="L1iCache" os_index="0" cpuset="0x00000003" complete_cpuset="0x00000003" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="8" cache_size="32768" depth="1" cache_linesize="64" cache_associativity="8" cache_type="2">
              <info name="Inclusive" value="0"/>
              <object type="Core" os_index="0" cpuset="0x00000003" complete_cpuset="0x00000003" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="2">
                <object type="PU" os_index="0" cpuset="0x00000001" complete_cpuset="0x00000001" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="6"/>
                <object type="PU" os_index="1" cpuset="0x00000002" complete_cpuset="0x00000002" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="11"/>
              </object>
            </object>
          </object>
        </object>
        <object type="L2Cache" os_index="1" cpuset="0x0000000c" complete_cpuset="0x0000000c" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="17" cache_size="2097152" depth="2" cache_linesize="64" cache_associativity="16" cache_type="0">
          <info name="Inclusive" value="0"/>
          <object type="L1Cache" os_index="4" cpuset="0x0000000c" complete_cpuset="0x0000000c" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="15" cache_size="49152" depth="1" cache_linesize="64" cache_associativity="12" cache_type="1">
            <info name="Inclusive" value="0"/>
            <object type="L1iCache" os_index="4" cpuset="0x0000000c" complete_cpuset="0x0000000c" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="16" cache_size="32768" depth="1" cache_linesize="64" cache_associativity="8" cache_type="2">
              <info name="Inclusive" value="0"/>
              <object type="Core" os_index="4" cpuset="0x0000000c" complete_cpuset="0x0000000c" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="12">
                <object type="PU" os_index="2" cpuset="0x00000004" complete_cpuset="0x00000004" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="14"/>
                <object type="PU" os_index="3" cpuset="0x00000008" complete_cpuset="0x00000008" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="18"/>
              </object>
            </object>
          </object>
        </object>
        <object type="L2Cache" os_index="2" cpuset="0x00000030" complete_cpuset="0x00000030" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="24" cache_size="2097152" depth="2" cache_linesize="64" cache_associativity="16" cache_type="0">
          <info name="Inclusive" value="0"/>
          <object type="L1Cache" os_index="8" cpuset="0x00000030" complete_cpuset="0x00000030" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="22" cache_size="49152" depth="1" cache_linesize="64" cache_associativity="12" cache_type="1">
            <info name="Inclusive" value="0"/>
            <object type="L1iCache" os_index="8" cpuset="0x00000030" complete_cpuset="0x00000030" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="23" cache_size="32768" depth="1" cache_linesize="64" cache_associativity="8" cache_type="2">
              <info name="Inclusive" value="0"/>
              <object type="Core" os_index="8" cpuset="0x00000030" complete_cpuset="0x00000030" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="19">
                <object type="PU" os_index="4" cpuset="0x00000010" complete_cpuset="0x00000010" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="21"/>
                <object type="PU" os_index="5" cpuset="0x00000020" complete_cpuset="0x00000020" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="25"/>
              </object>
            </object>
          </object>
        </object>
        <object type="L2Cache" os_index="3" cpuset="0x000000c0" complete_cpuset="0x000000c0" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="31" cache_size="2097152" depth="2" cache_linesize="64" cache_associativity="16" cache_type="0">
          <info name="Inclusive" value="0"/>
          <object type="L1Cache" os_index="12" cpuset="0x000000c0" complete_cpuset="0x000000c0" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="29" cache_size="49152" depth="1" cache_linesize="64" cache_associativity="12" cache_type="1">
            <info name="Inclusive" value="0"/>
            <object type="L1iCache" os_index="12" cpuset="0x000000c0" complete_cpuset="0x000000c0" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="30" cache_size="32768" depth="1" cache_linesize="64" cache_associativity="8" cache_type="2">
              <info name="Inclusive" value="0"/>
              <object type="Core" os_index="12" cpuset="0x000000c0" complete_cpuset="0x000000c0" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="26">
                <object type="PU" os_index="6" cpuset="0x00000040" complete_cpuset="0x00000040" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="28"/>
                <object type="PU" os_index="7" cpuset="0x00000080" complete_cpuset="0x00000080" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="32"/>
              </object>
            </object>
          </object>
        </object>
        <object type="L2Cache" os_index="4" cpuset="0x00000300" complete_cpuset="0x00000300" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="38" cache_size="2097152" depth="2" cache_linesize="64" cache_associativity="16" cache_type="0">
          <info name="Inclusive" value="0"/>
          <object type="L1Cache" os_index="16" cpuset="0x00000300" complete_cpuset="0x00000300" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="36" cache_size="49152" depth="1" cache_linesize="64" cache_associativity="12" cache_type="1">
            <info name="Inclusive" value="0"/>
            <object type="L1iCache" os_index="16" cpuset="0x00000300" complete_cpuset="0x00000300" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="37" cache_size="32768" depth="1" cache_linesize="64" cache_associativity="8" cache_type="2">
              <info name="Inclusive" value="0"/>
              <object type="Core" os_index="16" cpuset="0x00000300" complete_cpuset="0x00000300" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="33">
                <object type="PU" os_index="8" cpuset="0x00000100" complete_cpuset="0x00000100" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="35"/>
                <object type="PU" os_index="9" cpuset="0x00000200" complete_cpuset="0x00000200" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="39"/>
              </object>
            </object>
          </object>
        </object>
        <object type="L2Cache" os_index="5" cpuset="0x00000c00" complete_cpuset="0x00000c00" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="45" cache_size="2097152" depth="2" cache_linesize="64" cache_associativity="16" cache_type="0">
          <info name="Inclusive" value="0"/>
          <object type="L1Cache" os_index="20" cpuset="0x00000c00" complete_cpuset="0x00000c00" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="43" cache_size="49152" depth="1" cache_linesize="64" cache_associativity="12" cache_type="1">
            <info name="Inclusive" value="0"/>
            <object type="L1iCache" os_index="20" cpuset="0x00000c00" complete_cpuset="0x00000c00" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="44" cache_size="32768" depth="1" cache_linesize="64" cache_associativity="8" cache_type="2">
              <info name="Inclusive" value="0"/>
              <object type="Core" os_index="20" cpuset="0x00000c00" complete_cpuset="0x00000c00" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="40">
                <object type="PU" os_index="10" cpuset="0x00000400" complete_cpuset="0x00000400" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="42"/>
                <object type="PU" os_index="11" cpuset="0x00000800" complete_cpuset="0x00000800" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="46"/>
              </object>
            </object>
          </object>
        </object>
        <object type="L2Cache" os_index="6" cpuset="0x0000f000" complete_cpuset="0x0000f000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="52" cache_size="4194304" depth="2" cache_linesize="64" cache_associativity="16" cache_type="0">
          <info name="Inclusive" value="0"/>
          <object type="L1Cache" os_index="24" cpuset="0x00001000" complete_cpuset="0x00001000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="50" cache_size="32768" depth="1" cache_linesize="64" cache_associativity="8" cache_type="1">
            <info name="Inclusive" value="0"/>
            <object type="L1iCache" os_index="24" cpuset="0x00001000" complete_cpuset="0x00001000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="51" cache_size="65536" depth="1" cache_linesize="64" cache_associativity="8" cache_type="2">
              <info name="Inclusive" value="0"/>
              <object type="Core" os_index="24" cpuset="0x00001000" complete_cpuset="0x00001000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="47">
                <object type="PU" os_index="12" cpuset="0x00001000" complete_cpuset="0x00001000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="49"/>
              </object>
            </object>
          </object>
          <object type="L1Cache" os_index="25" cpuset="0x00002000" complete_cpuset="0x00002000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="55" cache_size="32768" depth="1" cache_linesize="64" cache_associativity="8" cache_type="1">
            <info name="Inclusive" value="0"/>
            <object type="L1iCache" os_index="25" cpuset="0x00002000" complete_cpuset="0x00002000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="56" cache_size="65536" depth="1" cache_linesize="64" cache_associativity="8" cache_type="2">
              <info name="Inclusive" value="0"/>
              <object type="Core" os_index="25" cpuset="0x00002000" complete_cpuset="0x00002000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="53">
                <object type="PU" os_index="13" cpuset="0x00002000" complete_cpuset="0x00002000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="54"/>
              </object>
            </object>
          </object>
          <object type="L1Cache" os_index="26" cpuset="0x00004000" complete_cpuset="0x00004000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="59" cache_size="32768" depth="1" cache_linesize="64" cache_associativity="8" cache_type="1">
            <info name="Inclusive" value="0"/>
            <object type="L1iCache" os_index="26" cpuset="0x00004000" complete_cpuset="0x00004000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="60" cache_size="65536" depth="1" cache_linesize="64" cache_associativity="8" cache_type="2">
              <info name="Inclusive" value="0"/>
              <object type="Core" os_index="26" cpuset="0x00004000" complete_cpuset="0x00004000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="57">
                <object type="PU" os_index="14" cpuset="0x00004000" complete_cpuset="0x00004000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="58"/>
              </object>
            </object>
          </object>
          <object type="L1Cache" os_index="27" cpuset="0x00008000" complete_cpuset="0x00008000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="63" cache_size="32768" depth="1" cache_linesize="64" cache_associativity="8" cache_type="1">
            <info name="Inclusive" value="0"/>
            <object type="L1iCache" os_index="27" cpuset="0x00008000" complete_cpuset="0x00008000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="64" cache_size="65536" depth="1" cache_linesize="64" cache_associativity="8" cache_type="2">
              <info name="Inclusive" value="0"/>
              <object type="Core" os_index="27" cpuset="0x00008000" complete_cpuset="0x00008000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="61">
                <object type="PU" os_index="15" cpuset="0x00008000" complete_cpuset="0x00008000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="62"/>
              </object>
            </object>
          </object>
        </object>
        <object type="L2Cache" os_index="7" cpuset="0x000f0000" complete_cpuset="0x000f0000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="70" cache_size="4194304" depth="2" cache_linesize="64" cache_associativity="16" cache_type="0">
          <info name="Inclusive" value="0"/>
          <object type="L1Cache" os_index="28" cpuset="0x00010000" complete_cpuset="0x00010000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="68" cache_size="32768" depth="1" cache_linesize="64" cache_associativity="8" cache_type="1">
            <info name="Inclusive" value="0"/>
            <object type="L1iCache" os_index="28" cpuset="0x00010000" complete_cpuset="0x00010000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="69" cache_size="65536" depth="1" cache_linesize="64" cache_associativity="8" cache_type="2">
              <info name="Inclusive" value="0"/>
              <object type="Core" os_index="28" cpuset="0x00010000" complete_cpuset="0x00010000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="65">
                <object type="PU" os_index="16" cpuset="0x00010000" complete_cpuset="0x00010000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="67"/>
              </object>
            </object>
          </object>
          <object type="L1Cache" os_index="29" cpuset="0x00020000" complete_cpuset="0x00020000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="73" cache_size="32768" depth="1" cache_linesize="64" cache_associativity="8" cache_type="1">
            <info name="Inclusive" value="0"/>
            <object type="L1iCache" os_index="29" cpuset="0x00020000" complete_cpuset="0x00020000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="74" cache_size="65536" depth="1" cache_linesize="64" cache_associativity="8" cache_type="2">
              <info name="Inclusive" value="0"/>
              <object type="Core" os_index="29" cpuset="0x00020000" complete_cpuset="0x00020000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="71">
                <object type="PU" os_index="17" cpuset="0x00020000" complete_cpuset="0x00020000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="72"/>
              </object>
            </object>
          </object>
          <object type="L1Cache" os_index="30" cpuset="0x00040000" complete_cpuset="0x00040000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="77" cache_size="32768" depth="1" cache_linesize="64" cache_associativity="8" cache_type="1">
            <info name="Inclusive" value="0"/>
            <object type="L1iCache" os_index="30" cpuset="0x00040000" complete_cpuset="0x00040000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="78" cache_size="65536" depth="1" cache_linesize="64" cache_associativity="8" cache_type="2">
              <info name="Inclusive" value="0"/>
              <object type="Core" os_index="30" cpuset="0x00040000" complete_cpuset="0x00040000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="75">
                <object type="PU" os_index="18" cpuset="0x00040000" complete_cpuset="0x00040000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="76"/>
              </object>
            </object>
          </object>
          <object type="L1Cache" os_index="31" cpuset="0x00080000" complete_cpuset="0x00080000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="81" cache_size="32768" depth="1" cache_linesize="64" cache_associativity="8" cache_type="1">
            <info name="Inclusive" value="0"/>
            <object type="L1iCache" os_index="31" cpuset="0x00080000" complete_cpuset="0x00080000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="82" cache_size="65536" depth="1" cache_linesize="64" cache_associativity="8" cache_type="2">
              <info name="Inclusive" value="0"/>
              <object type="Core" os_index="31" cpuset="0x00080000" complete_cpuset="0x00080000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="79">
                <object type="PU" os_index="19" cpuset="0x00080000" complete_cpuset="0x00080000" nodeset="0x00000001" complete_nodeset="0x00000001" gp_index="80"/>
              </object>
            </object>
          </object>
        </object>
      </object>
    </object>
    <object type="Bridge" gp_index="98" bridge_type="0-1" depth="0" bridge_pci="0000:[00-06]">
      <object type="Bridge" gp_index="88" bridge_type="1-1" depth="1" bridge_pci="0000:[01-01]" pci_busid="0000:00:01.0" pci_type="0604 [8086:a70d] [1043:8882] 01" pci_link_speed="4.000000">
        <object type="PCIDev" gp_index="90" pci_busid="0000:01:00.0" pci_type="0300 [10de:2484] [1458:404c] a1" pci_link_speed="4.000000">
          <object type="OSDev" gp_index="102" name="opencl0d0" subtype="OpenCL" osdev_type="5">
            <info name="Backend" value="OpenCL"/>
            <info name="OpenCLDeviceType" value="GPU"/>
            <info name="GPUVendor" value="NVIDIA Corporation"/>
            <info name="GPUModel" value="NVIDIA GeForce RTX 3070"/>
            <info name="OpenCLPlatformIndex" value="0"/>
            <info name="OpenCLPlatformName" value="NVIDIA CUDA"/>
            <info name="OpenCLPlatformDeviceIndex" value="0"/>
            <info name="OpenCLComputeUnits" value="46"/>
            <info name="OpenCLGlobalMemorySize" value="8025152"/>
          </object>
        </object>
      </object>
      <object type="Bridge" gp_index="93" bridge_type="1-1" depth="1" bridge_pci="0000:[02-02]" pci_busid="0000:00:06.0" pci_type="0604 [8086:a74d] [1043:8882] 01" pci_link_speed="7.876923">
        <object type="PCIDev" gp_index="87" pci_busid="0000:02:00.0" pci_type="0108 [c0a9:5421] [c0a9:5021] 01" pci_link_speed="7.876923">
          <object type="OSDev" gp_index="99" name="nvme0n1" subtype="Disk" osdev_type="0">
            <info name="Size" value="1953514584"/>
            <info name="SectorSize" value="512"/>
            <info name="LinuxDeviceID" value="259:0"/>
            <info name="Model" value="CT2000P3PSSD8"/>
            <info name="Revision" value="P9CR40D"/>
            <info name="SerialNumber" value="2410E89C2371"/>
          </object>
        </object>
      </object>
      <object type="PCIDev" gp_index="96" pci_busid="0000:00:0e.0" pci_type="0104 [8086:a77f] [1043:8882] 00" pci_link_speed="0.000000"/>
      <object type="PCIDev" gp_index="89" pci_busid="0000:00:14.3" pci_type="0280 [8086:7a70] [8086:0094] 11" pci_link_speed="0.000000">
        <object type="OSDev" gp_index="100" name="wlp0s20f3" osdev_type="2">
          <info name="Address" value="c8:5e:a9:a4:34:8b"/>
        </object>
      </object>
      <object type="PCIDev" gp_index="84" pci_busid="0000:00:17.0" pci_type="0106 [8086:7a62] [1043:8882] 11" pci_link_speed="0.000000"/>
      <object type="Bridge" gp_index="97" bridge_type="1-1" depth="1" bridge_pci="0000:[05-05]" pci_busid="0000:00:1c.2" pci_type="0604 [8086:7a3a] [1043:8882] 11" pci_link_speed="0.615385">
        <object type="PCIDev" gp_index="94" pci_busid="0000:05:00.0" pci_type="0200 [8086:125c] [1043:8867] 06" pci_link_speed="0.615385">
          <object type="OSDev" gp_index="101" name="enp5s0" osdev_type="2">
            <info name="Address" value="10:7c:61:3d:81:8f"/>
          </object>
        </object>
      </object>
    </object>
  </object>
  <support name="discovery.pu"/>
  <support name="discovery.numa"/>
  <support name="discovery.numa_memory"/>
  <support name="discovery.disallowed_pu"/>
  <support name="discovery.disallowed_numa"/>
  <support name="discovery.cpukind_efficiency"/>
  <support name="cpubind.set_thisproc_cpubind"/>
  <support name="cpubind.get_thisproc_cpubind"/>
  <support name="cpubind.set_proc_cpubind"/>
  <support name="cpubind.get_proc_cpubind"/>
  <support name="cpubind.set_thisthread_cpubind"/>
  <support name="cpubind.get_thisthread_cpubind"/>
  <support name="cpubind.set_thread_cpubind"/>
  <support name="cpubind.get_thread_cpubind"/>
  <support name="cpubind.get_thisproc_last_cpu_location"/>
  <support name="cpubind.get_proc_last_cpu_location"/>
  <support name="cpubind.get_thisthread_last_cpu_location"/>
  <support name="membind.set_thisthread_membind"/>
  <support name="membind.get_thisthread_membind"/>
  <support name="membind.set_area_membind"/>
  <support name="membind.get_area_membind"/>
  <support name="membind.alloc_membind"/>
  <support name="membind.firsttouch_membind"/>
  <support name="membind.bind_membind"/>
  <support name="membind.interleave_membind"/>
  <support name="membind.weighted_interleave_membind"/>
  <support name="membind.migrate_membind"/>
  <support name="membind.get_area_memlocation"/>
  <support name="custom.exported_support"/>
  <cpukind cpuset="0x000ff000" forced_efficiency="0">
    <info name="FrequencyMaxMHz" value="3900"/>
    <info name="FrequencyBaseMHz" value="2600"/>
    <info name="LinuxCapacity" value="1024"/>
    <info name="CoreType" value="IntelAtom"/>
  </cpukind>
  <cpukind cpuset="0x00000fff" forced_efficiency="0">
    <info name="FrequencyMaxMHz" value="5100"/>
    <info name="FrequencyBaseMHz" value="3500"/>
    <info name="LinuxCapacity" value="1024"/>
    <info name="CoreType" value="IntelCore"/>
  </cpukind>
</topology>
)";
