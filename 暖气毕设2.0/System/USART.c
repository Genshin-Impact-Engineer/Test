/**
 * @file    USART.c
 * @brief   串口驱动实现文件
 * @note    本项目使用3个串口：
 *          - USART1: PA9(TX) / PA10(RX) - 蓝牙通信
 *          - USART2: PA2(TX) / PA3(RX) - 语音模块通信
 *          - USART3: PB10(TX) / PB11(RX) - 备用（用于printf调试）
 * @author  开发者
 * @date    2025
 */

#include "usart.h"
#include "stdarg.h"
#include "stdio.h"
#include "string.h"

/****************************************************************/
/* 以下是标准库支持，用于实现printf函数而不使用半主机模式         */
/* 这段代码是固定的，不要修改                                    */
#pragma import(__use_no_semihosting)

/* 定义一个FILE结构体，满足printf函数的依赖 */
struct __FILE
{
    int a;
};

/* 标准输出文件句柄 */
FILE __stdout;

/**
 * @brief   系统退出函数（固定代码）
 * @param   x: 退出码
 */
void _sys_exit(int x)
{
}

/**
 * @brief   字符输出函数（用于printf）
 * @param   ch: 要发送的字符
 * @param   f: 文件指针
 * @return  发送的字符
 * @note    通过USART3发送数据，用于printf重定向
 */
int fputc(int ch, FILE *f)
{
    /* 等待上次发送完成 */
    while(USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET);
    /* 发送数据 */
    USART_SendData(USART3, (unsigned char)ch);
    return ch;
}

/*====================================================================*/
/*                        USART1 蓝牙通信                              */
/*====================================================================*/

/* USART1接收缓冲区，最大255字节，最后一个字节存放换行符 */
u8  USART1_RX_BUF[USART1_REC_LEN];
/* USART1接收状态标志寄存器 */
u16 USART1_RX_STA;

/**
 * @brief   USART1串口初始化（蓝牙模块）
 * @param   bound: 波特率，如9600、115200等
 * @note    USART1使用PA9(TX)和PA10(RX)引脚
 *          需要在main.c中调用：Usart1_Init(波特率);
 */
void Usart1_Init(u32 bound)
{
    /* 第1步：使能时钟 */
    /* 使能GPIOA时钟（PA9、PA10所在端口） */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    /* 使能USART1时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
    /* 使能AFIO复用功能时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

    /* 第2步：复位USART1 */
    USART_DeInit(USART1);

    /* 第3步：配置GPIO引脚 */
    GPIO_InitTypeDef GPIO_InitStructure;
    
    /* 配置PA9为复用推挽输出（USART1_TX发送引脚） */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;           /* TX引脚：PA9 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;   /* 输出速度50MHz */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;     /* 复用推挽输出 */
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    /* 配置PA10为浮空输入（USART1_RX接收引脚） */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;          /* RX引脚：PA10 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING; /* 浮空输入 */
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 第4步：配置USART1参数 */
    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate = bound;              /* 波特率 */
    USART_InitStructure.USART_WordLength = USART_WordLength_8b; /* 数据位8位 */
    USART_InitStructure.USART_StopBits = USART_StopBits_1;    /* 1位停止位 */
    USART_InitStructure.USART_Parity = USART_Parity_No;      /* 无校验位 */
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None; /* 无硬件流控制 */
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx; /* 收发模式 */
    USART_Init(USART1, &USART_InitStructure);

    /* 第5步：配置中断（本项目未启用USART1接收中断） */
    USART_ITConfig(USART1, USART_IT_RXNE, DISABLE); /* 不使能接收中断 */
    USART_ClearFlag(USART1, USART_FLAG_TC);          /* 清除发送完成标志 */

    /* 配置NVIC中断优先级 */
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;          /* USART1中断通道 */
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;  /* 抢占优先级2 */
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;         /* 子优先级2 */
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;            /* 使能中断 */
    NVIC_Init(&NVIC_InitStructure);

    /* 第6步：使能USART1 */
    USART_Cmd(USART1, ENABLE);
}

/*====================================================================*/
/*                        USART2 语音模块通信                          */
/*====================================================================*/

/* USART2接收缓冲区 */
u8  USART2_RX_BUF[USART2_REC_LEN];
/* USART2发送缓冲区 */
u8  USART2_TX_BUF[255];
/* USART2接收状态标志 */
u16 USART2_RX_STA;
/* 接收数据计数 */
u8 RX_BUF_CNT = 0;

/**
 * @brief   USART2串口初始化（语音模块）
 * @param   bound: 波特率
 * @note    USART2使用PA2(TX)和PA3(RX)引脚
 */
void Usart2_Init(u32 bound)
{
    /* 第1步：使能时钟 */
    /* 使能GPIOA时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    /* 使能USART2时钟（USART2在APB1总线上） */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    /* 使能AFIO复用功能时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    
    /* 第2步：复位USART2 */
    USART_DeInit(USART2);
    
    /* 第3步：配置GPIO引脚 */
    GPIO_InitTypeDef GPIO_InitStructure;
    
    /* 配置PA2为复用推挽输出（USART2_TX发送引脚） */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;             /* TX引脚：PA2 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;      /* 复用推挽输出 */
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    /* 配置PA3为浮空输入（USART2_RX接收引脚） */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;             /* RX引脚：PA3 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING; /* 浮空输入 */
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    /* 第4步：配置USART2参数 */
    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate = bound;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &USART_InitStructure);
    
    /* 第5步：配置中断（使能接收中断，用于接收语音模块反馈） */
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE); /* 使能接收中断 */
    USART_ClearFlag(USART2, USART_FLAG_TC);
    
    /* 配置NVIC中断优先级 */
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;    /* 子优先级比USART1高 */
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    
    /* 第6步：使能USART2 */
    USART_Cmd(USART2, ENABLE);
}

/**
 * @brief   USART2发送字符串函数（用于语音模块）
 * @param   str: 要发送的字符串指针
 * @note    发送完字符串后会自动发送换行符\r\n
 *          用于向语音模块发送控制指令
 */
void Usart2_Send_String(char* str)
{
    /* 逐字符发送，直到遇到字符串结束符\0 */
    while(*str) {
        /* 等待上次发送完成 */
        while(USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
        /* 发送一个字符 */
        USART_SendData(USART2, *str);
        str++;
    }
    /* 发送换行符 */
    while(USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
    USART_SendData(USART2, '\r');
    while(USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
    USART_SendData(USART2, '\n');
}

/**
 * @brief   USART2接收中断服务函数
 * @note    当USART2接收到数据时触发此中断
 *          将接收到的数据存入USART2_RX_BUF缓冲区
 */
void USART2_IRQHandler(void)
{
    u8 Res;
    
    /* 判断是否接收到数据（RXNE标志） */
    if(USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) {
        /* 读取接收到的数据 */
        Res = USART_ReceiveData(USART2);
        /* 存入接收缓冲区 */
        USART2_RX_BUF[RX_BUF_CNT] = Res;
        RX_BUF_CNT++;
        /* 防止缓冲区溢出 */
        if(RX_BUF_CNT >= 200) {
            RX_BUF_CNT = 199;
        }
    }
}

/*====================================================================*/
/*                        USART3 备用通信                              */
/*====================================================================*/

/* USART3接收缓冲区 */
u8  USART3_RX_BUF[USART3_REC_LEN];
/* USART3接收状态标志 */
u16 USART3_RX_STA;

/**
 * @brief   USART3串口初始化（备用/调试）
 * @param   bound: 波特率
 * @note    USART3使用PB10(TX)和PB11(RX)引脚
 */
void Usart3_Init(u32 bound)
{
    /* 第1步：使能时钟 */
    /* 使能GPIOB时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    /* 使能USART3时钟（USART3在APB1总线上） */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
    /* 使能AFIO复用功能时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    
    /* 第2步：复位USART3 */
    USART_DeInit(USART3);
    
    /* 第3步：配置GPIO引脚 */
    GPIO_InitTypeDef GPIO_InitStructure;
    
    /* 配置PB10为复用推挽输出（USART3_TX发送引脚） */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;            /* TX引脚：PB10 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;      /* 复用推挽输出 */
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    
    /* 配置PB11为浮空输入（USART3_RX接收引脚） */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;            /* RX引脚：PB11 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING; /* 浮空输入 */
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    
    /* 第4步：配置USART3参数 */
    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate = bound;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART3, &USART_InitStructure);
    
    /* 第5步：配置中断 */
    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
    USART_ClearFlag(USART3, USART_FLAG_TC);
    
    /* 配置NVIC中断优先级 */
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    
    /* 第6步：使能USART3 */
    USART_Cmd(USART3, ENABLE);
}

/**
 * @brief   USART3接收中断服务函数
 */
void USART3_IRQHandler(void)
{
    u8 Res;
    
    if(USART_GetITStatus(USART3, USART_IT_RXNE) != RESET) {
        Res = USART_ReceiveData(USART3);
        USART3_RX_BUF[USART3_RX_STA & 0x7FFF] = Res;
        USART3_RX_STA++;
        if((USART3_RX_STA & 0x8000) == 0) {
            if(USART3_RX_STA > USART3_REC_LEN - 1) {
                USART3_RX_STA = 0;
            }
        }
    }
}

/*====================================================================*/
/*                          辅助函数                                   */
/*====================================================================*/

/**
 * @brief   清除串口缓冲区
 * @note    将所有接收缓冲区的数据清零
 */
void Clear_USART_buf(void)
{
    memset(USART1_RX_BUF, 0, USART1_REC_LEN);
    memset(USART2_RX_BUF, 0, USART2_REC_LEN);
    memset(USART3_RX_BUF, 0, USART3_REC_LEN);
    USART1_RX_STA = 0;
    USART2_RX_STA = 0;
    USART3_RX_STA = 0;
    RX_BUF_CNT = 0;
}
