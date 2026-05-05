## ASR-PRO语音播报功能

串口2，波特率115200
状态收发表如下
状态\收发         STM32-TX->ASR-PRO-RX        ASR-PRO-TX->STM32-RX(正确接收后回复)
更新完成           completed                    completed
取消更新           cancelled                    cancelled
超重，请重新称量    Overweight                  Overweight
重量异常，请重新称量  Weight abnormal             Weight abnormal
去皮                Tare                            Tare
取消去皮            Tare Off                    Tare Off
称量完成            Measure             Measure
开始调整            Start Adjust                Start Adjust
## ps：
当stm32正确发送后，asr-pro会回复收到的消息，其他状况下一律不回复
更新完成、取消更新语音只播报一次
超重，请重新称量\重量异常，请重新称量:
处于报警界面，每隔3s播报一次，
退出报警界面，但依旧处于报警状态，蜂鸣器刚鸣叫时，播报一次
超重，请重新称量的权重比重量异常，请重新称量权重高，也就说是，同时发生，只触发超重，请重新称量的语音播报
执行去皮与取消去皮操作时，进行相应的播报一次
按下K2，执行称量完成的播报一次