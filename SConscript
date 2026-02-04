from building import *
import rtconfig
import os

cwd = GetCurrentDir()
src = []
CPPPATH = []
CPPDEFINES = []
LOCAL_CFLAGS = ''
LOCAL_CCFLAGS = ''
LIBS = []
LIBPATH = []

# 获取RT-Thread根目录路径
bsp_dir = os.path.normpath(os.path.join(cwd, '..', '..'))
rtthread_root = os.path.normpath(os.path.join(bsp_dir,'rt-thread'))
if not os.path.exists(rtthread_root):
    rtthread_root = os.path.normpath(os.path.join(bsp_dir, '..', '..', '..','..'))

# 优先添加本地 mbedtls 3.x 头文件路径（必须在 ROM mbedtls 2.17 之前）
# 这样可以确保编译时使用 mbedtls 3.x 的头文件而不是 ROM 中的 2.17 版本
mbedtls_local_path = cwd + '/src/mbedtls/mbedtls/include'
if os.path.exists(mbedtls_local_path):
    CPPPATH += [
        mbedtls_local_path,
        cwd + '/src/mbedtls/mbedtls/library',  # common.h 等内部头文件
    ]

# 公共头文件路径
CPPPATH += [
    cwd + '/inc',
    cwd + '/src/macsw/export',
    cwd + '/src/macsw/import',
    cwd + '/src/wifi_manager',
    cwd + '/src/wifi_manager/wpas',
    cwd + '/src/plf/src',
    cwd + '/src/plf/riscv/arch',
    cwd + '/src/plf/riscv/arch/boot',
    cwd + '/src/plf/riscv/arch/compiler',
    cwd + '/src/plf/riscv/arch/ll',
    cwd + '/src/plf/riscv/arch/lib',
    cwd + '/src/plf/riscv/gd32vw55x',
    cwd + '/src/plf/riscv/NMSIS/Include',
    cwd + '/src/plf/riscv/NMSIS/DSP/Include',
    cwd + '/src/rtos/rtos_wrapper',
    cwd + '/src/util/include',
    cwd + '/rom_export/halcomm',
    cwd + '/rom_export/bootloader',
    os.path.join(rtthread_root, 'components', 'net', 'netdev', 'include'),
]

# ROM mbedtls 2.17 头文件路径（仅在没有本地 mbedtls 3.x 时使用）
# 如果存在本地 mbedtls，这个路径会被忽略（因为符号已经被本地版本提供）
if not os.path.exists(mbedtls_local_path):
    CPPPATH += [cwd + '/rom_export/mbedtls-2.17.0-rom/include']

if GetDepend(['PKG_USING_GD32VW55X_WIFI']):

    # 公共定义
    CPPDEFINES += ['CFG_RTOS', 'CONFIG_WIFI_SUPPORT']

    # wifi_manager 源文件
    src += Glob('src/wifi_manager/*.c')
    # wpas 源文件（排除 wpas_sae_crypto_mbedtls.c，它被 wifi_wpa.c 包含）
    wpas_src = Glob('src/wifi_manager/wpas/*.c')
    wpas_src = [f for f in wpas_src if not f.rstr().endswith('wpas_sae_crypto_mbedtls.c')]
    src += wpas_src

    # 平台层源文件 (需要根据实际移植的文件调整)
    if os.path.exists(cwd + '/src/plf/src'):
        # 排除 init_rom_symbol.c（避免与 init_rom.c 重复定义 rom_symbol_init）
        plf_src = Glob('src/plf/src/*.c')
        plf_src = [f for f in plf_src if not f.rstr().endswith('init_rom_symbol.c')]
        src += plf_src
        # 平台子模块
        plf_modules = ['rf', 'dma', 'nvds', 'uart', 'time', 'trng', 'qspi_flash', 'raw_flash', 'wdt', 'spi', 'spi_i2s', 'reg']
        for module in plf_modules:
            module_path = cwd + '/src/plf/src/' + module
            if os.path.exists(module_path):
                src += Glob('src/plf/src/' + module + '/*.c')
                CPPPATH += [cwd + '/src/plf/src/' + module]

    # RISC-V 架构特定文件（中断处理、启动代码等）
    riscv_platform_path = cwd + '/src/plf/riscv/gd32vw55x'
    if os.path.exists(riscv_platform_path):
        riscv_src = Glob('src/plf/riscv/gd32vw55x/*.c')
        # 排除与 RT-Thread 或标准外设库冲突的文件
        exclude_files = [
            'system_gd32vw55x.c',   # 系统初始化函数与标准外设库冲突
        ]
        riscv_src = [f for f in riscv_src if not any(f.rstr().endswith(ex) for ex in exclude_files)]
        src += riscv_src

    # RTOS适配层
    if os.path.exists(cwd + '/src/rtos/rtos_wrapper'):
        src += Glob('src/rtos/rtos_wrapper/*.c')

    # 工具库
    if os.path.exists(cwd + '/src/util/src'):
        src += Glob('src/util/src/*.c')

    # mbedtls 库（如果存在本地源码）
    mbedtls_library_path = cwd + '/src/mbedtls/mbedtls/library'
    if os.path.exists(mbedtls_library_path):
        # 编译 mbedtls 核心库文件
        mbedtls_src = Glob('src/mbedtls/mbedtls/library/*.c')
        # 排除一些不需要的文件（可选，根据实际需求调整）
        exclude_files = [
            'net_sockets.c',  # 网络套接字（使用 LWIP）
            'timing.c',       # 时序函数（可能需要平台适配）
        ]
        mbedtls_src = [f for f in mbedtls_src if not any(f.rstr().endswith(ex) for ex in exclude_files)]
        src += mbedtls_src

        # mbedtls 头文件路径已在文件开头添加（在 ROM mbedtls 之前）

        # mbedtls 需要的宏定义
        # mbedtls 3.x 使用 mbedtls_config.h 作为默认配置文件
        # 不需要显式指定 MBEDTLS_CONFIG_FILE，因为build_info.h会自动包含
        CPPDEFINES += [
            #'MBEDTLS_CONFIG_FILE=<mbedtls/mbedtls_config.h>',  # 注释掉，使用默认
            # RT-Thread 移植：避免包含 <time.h> 导致的头文件循环包含
            # 定义 time_t 类型为 long，避免触发 newlib 和 RT-Thread libc 的冲突
            'MBEDTLS_PLATFORM_TIME_TYPE_MACRO=long',
            # RT-Thread 移植：避免 x509_crt.c 包含 <sys/socket.h> 和 <arpa/inet.h>
            # 这会导致 LwIP 和 netdev 的 IP 地址类型定义冲突
            # 强制使用 mbedtls 内置的软件实现 inet_pton
            'MBEDTLS_TEST_SW_INET_PTON',
        ]

    # 强制启用 lwip netif 回调（GD32 WiFi 需要）
    CPPDEFINES += [
        'LWIP_NETIF_STATUS_CALLBACK=1',
        'LWIP_NETIF_LINK_CALLBACK=1',
    ]

    if GetDepend(['RT_USING_NETDEV']):
        src += [
            'port/wifi_netif_port.c'
        ]
    
    lwip_path = cwd + '/src/lwip/port'
    if os.path.exists(lwip_path):

        src += [
            'src/lwip/port/dhcpd.c',
            'src/lwip/port/sys_arch.c',
            'src/lwip/port/wifi_netif.c',
            'src/lwip/port/dnsd.c',
        ]

        # LwIP 头文件路径（优先级最高，覆盖 RT-Thread 的 lwip）
        CPPPATH = [
            cwd + '/src/lwip/port',
            cwd + '/src/lwip/port/arch',
        ] + CPPPATH

        # LwIP 相关宏定义
        CPPDEFINES += [
            'LWIP_TIMEVAL_PRIVATE=0',  # 避免与系统 timeval 冲突
        ]

        # 添加 lwip mem.c 编译（RT-Thread 的 lwip SConscript 没有包含它）
        # GD32 的 lwipopts.h 定义了 MEM_LIBC_MALLOC=1，但仍需要 mem_init 等函数
        lwip_mem_c = os.path.join(rtthread_root,'components', 'net', 'lwip', 'lwip-2.1.2', 'src', 'core', 'mem.c')
        if os.path.exists(lwip_mem_c):
            src += [lwip_mem_c]

    # 预编译库配置
    lib_path = cwd + '/lib'
    dsp_lib_path = cwd + '/src/plf/riscv/NMSIS/Library/DSP/GCC'

    if os.path.exists(lib_path):
        LIBPATH = [lib_path]
        # 需要链接所有预编译库：wifi, wpas, rf
        # 注意：虽然本地编译了 mbedtls 3.x，但 wpas 源码不完整，仍需要 libwpas.a
        # 链接器会优先使用本地编译的 mbedtls 符号，libwpas.a 提供 wpas 特定函数
        LIBS = ['wifi', 'wpas', 'rf']

        # 添加 NMSIS DSP 库（用于 RF 校准和信号处理）
        if os.path.exists(dsp_lib_path):
            LIBPATH.append(dsp_lib_path)
            # 根据当前架构选择对应的 DSP 库：rv32imafc
            LIBS += ['nmsis_dsp_rv32imafc']

        # 添加必要的系统库（用于浮点运算）
        # libgcc 提供浮点运算支持
        if rtconfig.PLATFORM == 'gcc':
            LIBS += ['gcc']  # 链接 libgcc.a

    # ROM 符号表链接（已在 rtconfig.py 的 LFLAGS 中全局添加）

    # 配置选项
    if GetDepend(['GD32VW55X_WIFI_STATION_MODE']):
        CPPDEFINES += ['CONFIG_WIFI_STA_MODE']

    if GetDepend(['GD32VW55X_WIFI_SOFTAP_MODE']):
        CPPDEFINES += ['CONFIG_WIFI_SOFTAP_MODE']

    if GetDepend(['GD32VW55X_WIFI_WPA_SECURITY']):
        CPPDEFINES += ['CONFIG_WPA_SECURITY']

    if GetDepend(['GD32VW55X_WIFI_WPA3_SECURITY']):
        CPPDEFINES += ['CONFIG_WPA3_SECURITY', 'CONFIG_SAE']

    if GetDepend(['GD32VW55X_WIFI_WPS_SUPPORT']):
        CPPDEFINES += ['CONFIG_WPS']

    if GetDepend(['GD32VW55X_WIFI_DEBUG']):
        CPPDEFINES += ['CONFIG_WIFI_DEBUG']
        debug_level = GetConfigValue('GD32VW55X_WIFI_DEBUG_LEVEL')
        if debug_level:
            CPPDEFINES += ['CONFIG_WIFI_DEBUG_LEVEL=' + str(debug_level)]

    if GetDepend(['GD32VW55X_WIFI_POWER_SAVE']):
        CPPDEFINES += ['CONFIG_WIFI_POWER_SAVE']

    # 示例代码
# 定义组
group = DefineGroup('gd32vw55x-wifi', src,
                    depend=['PKG_USING_GD32VW55X_WIFI'],
                    CPPPATH=CPPPATH,
                    CPPDEFINES=CPPDEFINES,
                    LIBS=LIBS,
                    LIBPATH=LIBPATH,
                    LOCAL_CFLAGS=LOCAL_CFLAGS,
                    LOCAL_CCFLAGS=LOCAL_CCFLAGS)

Return('group')
