#ifndef __SYSCALL_H__
#define __SYSCALL_H__

#define SYS_NULL						0
#define SYS_TEXTOUT						1
#define SYS_PAUSE_THREAD				2
#define SYS_GET_THREAD_ID				3
#define SYS_GET_PROCESS_ID				4
#define SYS_PROCESS_EXIT				5
#define SYS_PROCESS_WAIT				6
#define SYS_CREATE_PROCESS				7
#define SYS_PROCESS_LOAD_EXEC			8
#define SYS_CREATE_SHARED_MEM			9
#define SYS_OBTAIN_SHARED_MEM			10
#define SYS_UNMAP_SHARED_MEM			11
#define SYS_OPEN_FILE					12
#define SYS_CREATE_MEM_MAPPING			13
#define SYS_UNMAP_MEM_MAPPING			14
#define SYS_GET_PROCESS_HEAP_MEM		15
#define SYS_READ_FILE					16
#define SYS_WRITE_FILE					17
#define SYS_CREATE_DIR					18
#define SYS_REMOVE_FILE					19
#define SYS_CLOSE_FILE					20
#define SYS_FILE_IO_CONTROL				21
#define SYS_FILE_STAT					22
#define SYS_PROCESS_SLEEP				23
#define SYS_SIGNAL_RETURN				24
#define SYS_SET_SIGNAL					25
#define SYS_GET_SYSTEM_TIMER_TICK		26
#define SYS_GET_FONT_ID					27
#define SYS_GET_NUM_FONTS				28
#define SYS_GET_FONT_SIZE				29
#define SYS_MEM_MAP_DIRTY				30
#define SYS_CREATE_TTY					31
#define SYS_CREATE_USER_THREAD			32
#define SYS_SET_FILE_TO_PROCESS			33
#define SYS_PROCESS_HEAP_UNMAP			34
#define SYS_SEND_SIGNAL					35
#define SYS_GET_CURRENT_TIME			36
#define SYS_OPEN_DIR					37
#define SYS_READ_DIR					38
#define SYS_CREATE_TIMER				39
#define SYS_START_TIMER					40
#define SYS_STOP_TIMER					41
#define SYS_DESTROY_TIMER				42
#define SYS_GET_FILE_DESC				43
#define SYS_FILE_SET_OFFSET				44
#define SYS_GET_TIME_OF_DAY				45
#define SYS_CREATE_SOCKET				46
#define SYS_NET_CONNECT					47
#define SYS_NET_SEND					48
#define SYS_NET_RECEIVE					49
#define SYS_SOCKET_SET_OPT				50
#define SYS_NET_BIND					51
#define SYS_NET_ACCEPT					52
#define SYS_NET_LISTEN					53
#define SYS_CREATE_PIPE					54
#define SYS_GET_VDISK_INFO				55
#define SYS_GET_VDISK_PARTITION_INFO	56
#define SYS_GET_ENVIRONMENT_BLOCK		57
#define SYS_CRED_CHANGE_ID				58
#define SYS_CRED_ADD_SGROUP				59
#define SYS_CRED_SET_CAP				60
#define SYS_CRED_GET_CAP				61
#define SYS_SET_UID						62
#define SYS_SET_GID						63
#define SYS_CRED_GET_GROUP_ID			64
#define SYS_PROCESS_TOKEN_ADD_SELF		65
#define SYS_PROCESS_TOKEN_GET_THREAD_ID 66
#define SYS_PROCESS_TOKEN_REMOVE_SELF	67
#define SYS_POWER_DOWN					68
#define SYS_POWER_RESET					69
#define SYS_GET_CURRENT_US				70
#define SYS_GET_CURRENT_MS				71
#define SYS_ALARM						72
#define SYS_SET_ITIMER					73
#define SYS_GET_ITIMER					74
#define SYS_GET_NUM_PROCESS_COUNT		75
#define SYS_PROCESS_FETCH				76
#define SYS_SET_WALLTIME				77
#define SYS_GET_WALLTIME				78
#define SYS_EXIT_SUB_THREAD				79
#define SYS_SEND_SIGNAL_TO_THREAD		80
#define SYS_THREAD_WAIT_FOR_TERMINATION 81
#define SYS_KERNEL_UNAME				82
#define SYS_FILE_POLL					83

/* Extended native socket ABI. 84..178 stay reserved so older binaries
 * keep their numbers stable. */
#define SYS_SOCKETPAIR					179
#define SYS_GETSOCKOPT					180
#define SYS_GETSOCKNAME					181
#define SYS_GETPEERNAME					182
#define SYS_SHUTDOWN					183
#define SYS_SENDMMSG					186
#define SYS_RECVMMSG					187
#define SYS_ACCEPT4						188
#define SYS_SENDTO						190
#define SYS_RECVFROM					191
#define SYS_IF_NAMETOINDEX				192
#define SYS_GETIFADDRS					193

#define SYS_MAX_SYSCALL 195

/* Backward-compat alias */
#define AURORA_MAX_SYSCALL SYS_MAX_SYSCALL

#endif /* __SYSCALL_H__ */
