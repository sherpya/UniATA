
CROSSNT_DECL(
BOOLEAN,
PsGetVersion,(
    PULONG MajorVersion OPTIONAL,
    PULONG MinorVersion OPTIONAL,
    PULONG BuildNumber OPTIONAL,
    PUNICODE_STRING CSDVersion OPTIONAL
    ),
    (
    MajorVersion,
    MinorVersion,
    BuildNumber,
    CSDVersion
    ))

CROSSNT_DECL(
HANDLE,
PsGetCurrentProcessId,(),())

CROSSNT_DECL(
HANDLE,
PsGetCurrentThreadId,(),())

CROSSNT_DECL(
NTSTATUS,
NtQuerySystemInformation,(
    IN SYSTEM_INFORMATION_CLASS SystemInfoClass,
    OUT PVOID                   SystemInfoBuffer,
    IN ULONG                    SystemInfoBufferSize,
    OUT PULONG                  BytesReturned OPTIONAL
    ),(
    SystemInfoClass,
    SystemInfoBuffer,
    SystemInfoBufferSize,
    BytesReturned
))
