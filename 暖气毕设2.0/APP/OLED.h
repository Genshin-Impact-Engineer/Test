/**
 * @file    OLED.h
 * @brief   OLED显示屏驱动头文件
 * @note    OLED屏幕参数：
 *          - 屏幕尺寸：0.96寸
 *          - 分辨率：128x64像素
 *          - 通信接口：软件I2C
 *          - I2C引脚：PB8(SCL)、PB9(SDA)
 *          - I2C地址：0x78
 */

#ifndef __OLED_H
#define __OLED_H

#include <stdint.h>
#include "OLED_Data.h"

/*===========================================
 * 字体大小定义
 *===========================================*/

/* OLED_8X16:  8x16像素字体（英文字符8x16，中文字符16x16） */
#define OLED_8X16   8

/* OLED_6X8:  6x8像素字体 */
#define OLED_6X8    6

/*===========================================
 * 填充模式定义
 *===========================================*/

/* 不填充（空心） */
#define OLED_UNFILLED  0

/* 填充（实心） */
#define OLED_FILLED    1

/*===========================================
 * 函数声明
 *===========================================*/

/*-------------------- 基础操作函数 --------------------*/

/**
 * @brief   OLED初始化
 * @note    初始化I2C引脚、屏幕参数设置
 */
void OLED_Init(void);

/**
 * @brief   刷新显存到屏幕
 * @note    将显存数据发送到OLED显示
 */
void OLED_Update(void);

/**
 * @brief   局部刷新显存到屏幕
 * @param   X: 起始X坐标
 * @param   Y: 起始Y坐标
 * @param   Width: 宽度
 * @param   Height: 高度
 */
void OLED_UpdateArea(int16_t X, int16_t Y, uint8_t Width, uint8_t Height);

/*-------------------- 清屏函数 --------------------*/

/**
 * @brief   清屏（整个屏幕）
 */
void OLED_Clear(void);

/**
 * @brief   局部清屏
 * @param   X: 起始X坐标
 * @param   Y: 起始Y坐标
 * @param   Width: 宽度
 * @param   Height: 高度
 */
void OLED_ClearArea(int16_t X, int16_t Y, uint8_t Width, uint8_t Height);

/**
 * @brief   反转显示（整个屏幕）
 */
void OLED_Reverse(void);

/**
 * @brief   局部反转显示
 */
void OLED_ReverseArea(int16_t X, int16_t Y, uint8_t Width, uint8_t Height);

/*-------------------- 显示字符函数 --------------------*/

/**
 * @brief   显示单个字符
 * @param   X: X坐标
 * @param   Y: Y坐标
 * @param   Char: 要显示的字符
 * @param   FontSize: 字体大小（OLED_8X16 或 OLED_6X8）
 */
void OLED_ShowChar(int16_t X, int16_t Y, char Char, uint8_t FontSize);

/**
 * @brief   显示字符串
 * @param   X: X坐标
 * @param   Y: Y坐标
 * @param   String: 字符串指针
 * @param   FontSize: 字体大小
 */
void OLED_ShowString(int16_t X, int16_t Y, char *String, uint8_t FontSize);

/**
 * @brief   显示无符号整数
 * @param   X: X坐标
 * @param   Y: Y坐标
 * @param   Number: 要显示的数字
 * @param   Length: 显示位数
 * @param   FontSize: 字体大小
 */
void OLED_ShowNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);

/**
 * @brief   显示有符号整数（包含负数）
 */
void OLED_ShowSignedNum(int16_t X, int16_t Y, int32_t Number, uint8_t Length, uint8_t FontSize);

/**
 * @brief   显示十六进制数
 */
void OLED_ShowHexNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);

/**
 * @brief   显示二进制数
 */
void OLED_ShowBinNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);

/**
 * @brief   显示浮点数
 * @param   X: X坐标
 * @param   Y: Y坐标
 * @param   Number: 浮点数
 * @param   IntLength: 整数部分位数
 * @param   FraLength: 小数部分位数
 * @param   FontSize: 字体大小
 */
void OLED_ShowFloatNum(int16_t X, int16_t Y, double Number, uint8_t IntLength, uint8_t FraLength, uint8_t FontSize);

/**
 * @brief   显示图片
 * @param   X: X坐标
 * @param   Y: Y坐标
 * @param   Width: 图片宽度
 * @param   Height: 图片高度
 * @param   Image: 图片数据指针
 */
void OLED_ShowImage(int16_t X, int16_t Y, uint8_t Width, uint8_t Height, const uint8_t *Image);

/**
 * @brief   格式化打印（类似printf）
 */
void OLED_Printf(int16_t X, int16_t Y, uint8_t FontSize, char *format, ...);

/*-------------------- 绘图函数 --------------------*/

/**
 * @brief   画点
 * @param   X: X坐标（0~127）
 * @param   Y: Y坐标（0~63）
 */
void OLED_DrawPoint(int16_t X, int16_t Y);

/**
 * @brief   获取点状态
 * @return  1=亮, 0=灭
 */
uint8_t OLED_GetPoint(int16_t X, int16_t Y);

/**
 * @brief   画直线
 */
void OLED_DrawLine(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1);

/**
 * @brief   画矩形（空心）
 */
void OLED_DrawRectangle(int16_t X, int16_t Y, uint8_t Width, uint8_t Height, uint8_t IsFilled);

/**
 * @brief   画圆形（空心）
 */
void OLED_DrawCircle(int16_t X, int16_t Y, uint8_t Radius, uint8_t IsFilled);

/**
 * @brief   画椭圆（空心）
 */
void OLED_DrawEllipse(int16_t X, int16_t Y, uint8_t A, uint8_t B, uint8_t IsFilled);

/**
 * @brief   画三角形
 * @param   IsFilled: 是否填充（0=空心，1=实心）
 */
void OLED_DrawTriangle(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1, int16_t X2, int16_t Y2, uint8_t IsFilled);

/*-------------------- I2C底层函数 --------------------*/

/**
 * @brief   I2C开始信号
 */
void OLED_I2C_Start(void);

/**
 * @brief   I2C停止信号
 */
void OLED_I2C_Stop(void);

/**
 * @brief   I2C发送一个字节
 * @param   Byte: 要发送的字节
 */
void OLED_I2C_SendByte(uint8_t Byte);

/**
 * @brief   OLED写命令
 * @param   Command: 命令字节
 */
void OLED_WriteCommand(uint8_t Command);

/**
 * @brief   OLED写数据
 * @param   Data: 数据指针
 * @param   Count: 数据长度
 */
void OLED_WriteData(uint8_t *Data, uint8_t Count);

#endif
