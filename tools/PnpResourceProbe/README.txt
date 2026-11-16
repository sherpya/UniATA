PnpResourceProbe - Windows 2000/XP resource-conflict diagnostic

Build on Linux:
  make

Run as administrator on 32-bit Windows XP:
  PnpResourceProbe.exe > pnp-resources.txt

By default, this selects present devices with Code 12, devices using the
UniATA service, and PCI 1078:0102. An optional case-insensitive substring
selects other device instance IDs, for example:
  PnpResourceProbe.exe "PCI\VEN_1078&DEV_0102" > pci-conflicts.txt

The tool only reads Configuration Manager and SetupAPI information. It
installs nothing, does not rescan, and changes no resources, registry values,
services or device states. It frees API handles, never configurations.

For every selected device it prints status/problem, allocated resources,
boot/forced configurations, and raw basic/filtered requirements. It queries
current conflicts for each concrete I/O, memory, IRQ or DMA descriptor and
prints the owner identity, or the legacy driver name when provided by XP.
Requirements with alternative choices are dumped but not queried as if all
alternatives were simultaneously assigned.

This queries the current arbiter state; it does not capture the original
failed start request. Microsoft documents omissions, notably conflicts
between multiple non-PnP devices served by the same driver. Zero conflicts
therefore cannot rule out UniATA legacy/PnP ownership problems. API errors
are printed as CONFIGRET values and cause a nonzero exit status; absence of
a configuration is normal. Exit 2 means no matching present devices or bad
command-line arguments. Use the entire output, including empty/error results.

The build targets i586, uses -O2 -s, disables SSE/MMX, and requires only
Windows DLLs (no separately installed compiler runtime). It has been cross
compiled, statically checked and used on XP SP3 to distinguish
failed BOOT-only nodes from successfully started devices with ALLOC resources.

API references:
https://learn.microsoft.com/en-us/windows/win32/api/cfgmgr32/nf-cfgmgr32-cm_query_resource_conflict_list
https://learn.microsoft.com/en-us/windows/win32/api/cfgmgr32/nf-cfgmgr32-cm_get_resource_conflict_detailsa
https://learn.microsoft.com/en-us/windows/win32/api/cfgmgr32/ns-cfgmgr32-conflict_details_a
