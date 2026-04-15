/**
 * @file    step_motor.c
 * @brief   步进电机驱动实现，控制28BYJ48步进电机
 * @note    硬件连接：PA4~PA7（四相八拍驱动）
 */

#include "step_motor.h"
#include "delay.h"

/************************** 全局变量定义（内部使用） **************************/
static u8 motor_dir = MOTOR_DIR_CW;    // 当前旋转：默认顺时针
static u16 motor_delay = 10;           // 步进间隔延时（ms），控制转速
static u32 motor_total_step = 0;       // 累计步数（main.c可获取）

// 阀门控制变量
static u8 valve_current_level = 0;    // 当前阀门等级
static u32 valve_target_step = 0;      // 目标步数
static u32 valve_remaining_step = 0;   // 剩余需要移动的步数
static u8 valve_step_idx = 0;          // 八拍步进索引
static u8 valve_auto_enabled = 1;      // 自动控制使能标志

/************************** 八拍驱动时序表 **************************/
/**
 * 28BYJ48 八拍驱动时序：
 * A → A+B → B → B+C → C → C+D → D → D+A
 */
static const u8 motor_step_table[8] = {
    0x01, // A相  0001
    0x03, // A+B相 0011
    0x02, // B相  0010
    0x06, // B+C相 0110
    0x04, // C相  0100
    0x0C, // C+D相 1100
    0x08, // D相  1000
    0x09  // D+A相 1001
};

/**
 * @brief   设置单拍输出
 * @param   step 拍序号（0~7）
 * @return  无
 * @note    根据时序表设置PA4~PA7电平，延时控制转速
 */
static void Motor_Set_Step(u8 step)
{
    // 设置A相电平（PA4）
    if(step & 0x01) GPIO_SetBits(MOTOR_PORT, MOTOR_A_PIN);
    else GPIO_ResetBits(MOTOR_PORT, MOTOR_A_PIN);

    // 设置B相电平（PA5）
    if(step & 0x02) GPIO_SetBits(MOTOR_PORT, MOTOR_B_PIN);
    else GPIO_ResetBits(MOTOR_PORT, MOTOR_B_PIN);

    // 设置C相电平（PA6）
    if(step & 0x04) GPIO_SetBits(MOTOR_PORT, MOTOR_C_PIN);
    else GPIO_ResetBits(MOTOR_PORT, MOTOR_C_PIN);

    // 设置D相电平（PA7）
    if(step & 0x08) GPIO_SetBits(MOTOR_PORT, MOTOR_D_PIN);
    else GPIO_ResetBits(MOTOR_PORT, MOTOR_D_PIN);

    delay_ms(motor_delay); // 步进延时，控制转速
}

/**
 * @brief   步进电机初始化
 * @param   无
 * @return  无
 * @note    配置PA4~PA7为推挽输出，初始化为低电平
 */
void Step_Motor_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    // 1. 使能GPIOA时钟
    RCC_APB2PeriphClockCmd(MOTOR_RCC, ENABLE);

    // 2. 配置PA4~PA7为推挽输出
    GPIO_InitStructure.GPIO_Pin = MOTOR_A_PIN | MOTOR_B_PIN | MOTOR_C_PIN | MOTOR_D_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(MOTOR_PORT, &GPIO_InitStructure);

    // 3. 初始状态：全部低电平，禁止线圈
    GPIO_ResetBits(MOTOR_PORT, MOTOR_A_PIN | MOTOR_B_PIN | MOTOR_C_PIN | MOTOR_D_PIN);

    // 4. 初始化参数
    motor_dir = MOTOR_DIR_CW;
    motor_delay = 10;    // 默认转速：10ms/拍
    motor_total_step = 0;
}

/**
 * @brief   设置电机旋转方向
 * @param   dir 旋转方向（MOTOR_DIR_CW顺时针 / MOTOR_DIR_CCW逆时针）
 * @return  无
 */
void Step_Motor_Set_Dir(u8 dir)
{
    if(dir == MOTOR_DIR_CW || dir == MOTOR_DIR_CCW)
    {
        motor_dir = dir;
    }
}

/**
 * @brief   设置电机转速
 * @param   rpm 目标转速（转/分钟）
 * @param   step_angle 步进角度（度）
 * @return  无
 * @note    计算公式：延时(ms)=60000/(总步数*转速)
 */
void Step_Motor_Set_Speed(float rpm, float step_angle)
{
    // 计算每转步数：360/步进角度
    u16 total_step_per_round = 360 / step_angle;
    motor_delay = 60000 / (total_step_per_round * rpm);

    // 限幅：延时最小5ms，最大50ms
    if(motor_delay < 5) motor_delay = 5;
    if(motor_delay > 50) motor_delay = 50;
}

/**
 * @brief   步进电机运行指定步数
 * @param   step_num 要运行的步数
 * @return  无
 * @note    根据方向执行八拍时序，完成后释放线圈
 */
void Step_Motor_Run_Step(u16 step_num)
{
    u16 i, j;
    for(i = 0; i < step_num; i++)
    {
        if(motor_dir == MOTOR_DIR_CW) // 顺时针：执行正序
        {
            for(j = 0; j < 8; j++)
            {
                Motor_Set_Step(motor_step_table[j]);
            }
        }
        else // 逆时针：执行逆序
        {
            for(j = 7; j != 0xff; j--) // 防止无符号减法溢出
            {
                Motor_Set_Step(motor_step_table[j]);
            }
        }
        motor_total_step++; // 累计步数+1
    }

    // 运行完成后释放线圈（保持状态）
    GPIO_ResetBits(MOTOR_PORT, MOTOR_A_PIN | MOTOR_B_PIN | MOTOR_C_PIN | MOTOR_D_PIN);
}

/**
 * @brief   步进电机连续运行/停止
 * @param   en 0=停止，非0=运行
 * @return  无
 * @note    调用一次执行一拍，需在主循环中持续调用
 */
void Step_Motor_Cont_Run(u8 en)
{
    static u8 step_idx = 0;

    if(en == 0) // 停止：释放线圈
    {
        GPIO_ResetBits(MOTOR_PORT, MOTOR_A_PIN | MOTOR_B_PIN | MOTOR_C_PIN | MOTOR_D_PIN);
        return;
    }

    // 运行中：执行一拍
    if(motor_dir == MOTOR_DIR_CW)
    {
        Motor_Set_Step(motor_step_table[step_idx]);
        step_idx = (step_idx + 1) % 8;
    }
    else
    {
        Motor_Set_Step(motor_step_table[step_idx]);
        step_idx = (step_idx - 1 + 8) % 8;
    }
    motor_total_step++;
}

/**
 * @brief   获取累计步数
 * @param   无
 * @return  累计运行的步数
 * @note    可用于main.c计算阀门位置
 */
u16 Step_Motor_Get_Total_Step(void)
{
    return (u16)motor_total_step; // 转u16供main.c显示
}

/****************************** 阀门控制函数 ******************************/

/**
 * @brief   获取当前阀门等级
 * @param   无
 * @return  当前阀门等级（0~6）
 */
u8 Valve_Get_Level(void)
{
    return valve_current_level;
}

/**
 * @brief   停止阀门移动
 * @param   无
 * @return  无
 */
void Valve_Stop(void)
{
    valve_remaining_step = 0;  // 立即停止剩余移动
    valve_auto_enabled = 0;    // 禁用自动控制
    GPIO_ResetBits(MOTOR_PORT, MOTOR_A_PIN | MOTOR_B_PIN | MOTOR_C_PIN | MOTOR_D_PIN);
}

/**
 * @brief   设置阀门等级
 * @param   level 目标等级（0~5）
 *          - 0级：完全关闭（0步）
 *          - 1级：约20%开度（420步）
 *          - 2级：约40%开度（840步）
 *          - 3级：约60%开度（1260步）
 *          - 4级：约80%开度（1680步）
 *          - 5级：完全打开（2100步）
 * @return  实际设置的等级
 */
u8 Valve_Set_Level(u8 level)
{
    u32 target_step;
    s32 step_diff;
    u16 move_step;
    
    // 限幅：确保等级在有效范围内
    if(level > VALVE_LEVEL_MAX) {
        level = VALVE_LEVEL_MAX;
    }
    
    // 计算目标步数（相对于零点）
    // 0级=0步, 1级=350步, 2级=700步, 3级=1050步, 4级=1400步, 5级=1750步
    target_step = (u32)level * VALVE_TOTAL_STEPS / VALVE_LEVEL_MAX;
    
    // 计算需要移动的步数
    step_diff = target_step - motor_total_step;
    
    // 如果已经到达目标位置，直接返回
    if(step_diff == 0) {
        valve_current_level = level;
        return level;
    }
    
    // 设置移动方向
    if(step_diff > 0) {
        // 需要增大角度：顺时针旋转
        Step_Motor_Set_Dir(MOTOR_DIR_CW);
        move_step = (u16)step_diff;
    } else {
        // 需要减小角度：逆时针旋转
        Step_Motor_Set_Dir(MOTOR_DIR_CCW);
        move_step = (u16)(-step_diff);
    }
    
    // 移动到目标位置
    Step_Motor_Run_Step(move_step);
    
    // 更新当前档位
    valve_current_level = level;
    
    return level;
}

/****************************** 阀门自动控制 ******************************/

/**
 * @brief   根据温差自动调节阀门档位
 * @param   actual_temp 实际温度（℃）
 * @param   target_temp 目标温度（℃）
 * @return  无
 * @note    温差 = 实际温度 - 目标温度
 *          温差死区：±0.5°C（温差在此范围内电机停止，保持当前位置）
 * 
 * ========== 温差与档位对照表 ==========
 * | 温差范围        | 档位 | 电机角度(相较于上电时0度而言) | 说明              |
 * |-----------------|------|---------|-------------------|
 * | <= -6.5C       | 5级  | 相对150度   | 阀门大开，快速加热 ，转动预定度数停止 |
 * | -6.5 ~ -3.5C | 4级  |  相对120度   | 阀门大开   ，转动预定度数停止        |
 * | -3.5 ~ -0.5C | 3级  | 相对90度    | 阀门中开    ，转动预定度数停止       |
 * | -0.5 ~ +0.5C | 保持 | --      | 【死区】电机停止    |
 * | +0.5 ~ +3.5C | 2级  | 相对60度    | 阀门中小开  ，转动预定度数停止       |
 * | +3.5 ~ +6.5C | 1级  | 相对30度    | 阀门小开 ，转动预定度数停止          |
 * | > +6.5C       | 0级  | 0度     | 阀门关闭    ，转动预定度数停止       |
 */
void Valve_Auto_Control(float actual_temp, float target_temp)
{
    float temp_diff;
    u8 target_level;
    u8 current_level;
    
    // 获取当前阀门等级
    current_level = Valve_Get_Level();
    
    // 计算温差
    temp_diff = actual_temp - target_temp;
    
    // 死区判断：温差在±0.5°C之间，停止阀门
    if(temp_diff >= -0.5f && temp_diff <= 0.5f) {
        // 死区范围内，立即停止阀门并禁用自动控制
        Valve_Stop();
        return;
    }
    
    // 根据温差确定阀门目标等级（0-5级）
    if(temp_diff <= -6.5f) {
        // 温差 <= -6.5°C：阀门全开
        target_level = 5;
    } 
    else if(temp_diff <= -3.5f) {
        // -6.5°C < 温差 <= -3.5°C：阀门4级
        target_level = 4;
    }
    else if(temp_diff <= -0.5f) {
        // -3.5°C < 温差 <= -0.5°C：阀门3级
        target_level = 3;
    }
    else if(temp_diff <= 3.5f) {
        // +0.5°C < 温差 <= +3.5°C：阀门2级
        target_level = 2;
    }
    else if(temp_diff <= 6.5f) {
        // +3.5°C < 温差 <= +6.5°C：阀门1级
        target_level = 1;
    }
    else {
        // 温差 > +6.5°C：阀门全关
        target_level = 0;
    }
    
    // 只有目标等级与当前等级不同时，才请求移动
    if(target_level != current_level) {
        // 请求设置阀门到目标等级（非阻塞）
        Valve_Request_Level(target_level);
    }
}

/****************************** 非阻塞阀门控制 ******************************/

/**
 * @brief   请求设置阀门等级（非阻塞，只设置目标）
 * @param   level 目标等级（0~5）
 * @return  无
 * @note    不会立即移动，只设置目标位置，由Valve_Process()分步执行
 */
void Valve_Request_Level(u8 level)
{
    u32 target_step;
    s32 step_diff;
    
    // 重新使能自动控制
    valve_auto_enabled = 1;
    
    // 限幅
    if(level > VALVE_LEVEL_MAX) {
        level = VALVE_LEVEL_MAX;
    }
    
    // 计算目标步数（相对于零点）
    // 0级=0步, 1级=350步, 2级=700步, 3级=1050步, 4级=1400步, 5级=1750步
    target_step = (u32)level * VALVE_TOTAL_STEPS / VALVE_LEVEL_MAX;
    
    // 计算需要移动的步数
    step_diff = target_step - motor_total_step;
    
    // 如果已经到达目标位置，直接返回
    if(step_diff == 0) {
        valve_current_level = level;
        valve_remaining_step = 0;
        return;
    }
    
    // 设置移动方向
    if(step_diff > 0) {
        Step_Motor_Set_Dir(MOTOR_DIR_CW);   // 顺时针增大角度
        valve_remaining_step = (u32)step_diff;
    } else {
        Step_Motor_Set_Dir(MOTOR_DIR_CCW);  // 逆时针减小角度
        valve_remaining_step = (u32)(-step_diff);
    }
    
    // 更新当前档位
    valve_current_level = level;
}

/**
 * @brief   阀门处理函数（非阻塞，每次移动少量步数）
 * @param   max_steps 每次最大移动步数
 * @return  u8 0=阀门已在目标位置, 1=阀门移动中
 * @note    应该在主循环中频繁调用，每次只移动少量步数，避免阻塞
 */
u8 Valve_Process(u8 max_steps)
{
    u8 i, j;
    
    // 如果没有剩余步数，直接返回
    if(valve_remaining_step == 0) {
        // 释放线圈
        GPIO_ResetBits(MOTOR_PORT, MOTOR_A_PIN | MOTOR_B_PIN | MOTOR_C_PIN | MOTOR_D_PIN);
        return 0;
    }
    
    // 如果自动控制被禁用（死区模式），停止移动
    if(!valve_auto_enabled) {
        valve_remaining_step = 0;
        GPIO_ResetBits(MOTOR_PORT, MOTOR_A_PIN | MOTOR_B_PIN | MOTOR_C_PIN | MOTOR_D_PIN);
        return 0;
    }
    
    // 每次最多移动max_steps步
    if(valve_remaining_step < max_steps) {
        max_steps = (u8)valve_remaining_step;
    }
    
    // 移动指定步数
    for(i = 0; i < max_steps; i++) {
        // 执行一拍
        for(j = 0; j < 8; j++) {
            Motor_Set_Step(motor_step_table[j]);
        }
        motor_total_step++;
        valve_remaining_step--;
        
        // 如果到达目标位置，停止
        if(valve_remaining_step == 0) {
            break;
        }
    }
    
    return 1;  // 阀门仍在移动中
}

/**
 * @brief   检查阀门是否在移动中
 * @param   无
 * @return  u8 0=静止, 1=移动中
 */
u8 Valve_Is_Moving(void)
{
    return (valve_remaining_step > 0) ? 1 : 0;
}
