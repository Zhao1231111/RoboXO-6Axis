import requests
import time
import json

# Python Gateway 的基础地址
BASE_URL = "http://127.0.0.1:8000/api/game"

def test_start_game():
    """测试让机器人准备拿画笔"""
    print("\n--- [1] 发送游戏开始指令 (抓取画笔) ---")
    try:
        response = requests.post(f"{BASE_URL}/start")
        print(f"Status Code: {response.status_code}")
        print(f"Response: {response.json()}")
    except Exception as e:
        print(f"请求失败: {e}")

def test_send_vision_state():
    """测试发送视觉识别到的棋盘状态"""
    print("\n--- [2] 发送视觉检测状态 (玩家落子) ---")
    
    # 模拟当前的棋盘: 0为空, 1为玩家(X), 2为机器(O)
    # 比如玩家刚在中间 (索引4) 画了一个 X
    payload = {
        "board": [0, 0, 0, 0, 1, 0, 0, 0, 0],
        "player_moved": True,
        "timestamp": time.time()
    }
    
    headers = {"Content-Type": "application/json"}
    
    try:
        response = requests.post(
            f"{BASE_URL}/vision/board_state", 
            data=json.dumps(payload),
            headers=headers
        )
        print(f"Status Code: {response.status_code}")
        print(f"Response: {response.json()}")
    except Exception as e:
        print(f"请求失败: {e}")

def test_reset_game():
    """测试游戏重置让机器人拿橡皮擦除"""
    print("\n--- [3] 发送游戏重置指令 (抓取橡皮并擦除) ---")
    try:
        response = requests.post(f"{BASE_URL}/reset")
        print(f"Status Code: {response.status_code}")
        print(f"Response: {response.json()}")
    except Exception as e:
        print(f"请求失败: {e}")

def test_drop_pen():
    """测试让机器人放下画笔"""
    print("\n--- [4] 发送放笔指令 ---")
    try:
        response = requests.post(f"{BASE_URL}/drop_pen")
        print(f"Status Code: {response.status_code}")
        print(f"Response: {response.json()}")
    except Exception as e:
        print(f"请求失败: {e}")

if __name__ == "__main__":
    print("========== 视觉端模拟测试脚本 ==========")
    print("请确保 Python Gateway (FastAPI) 已经在 8000 端口启动。")
    print("在开始测试前，底层 C++ RoboXORT 也应以 --ipc 模式启动并上电完毕。")
    
    while True:
        print("\n选择你要测试的操作:")
        print("1. 发送 /start (机器人拿笔)")
        print("2. 发送 /vision/board_state (玩家落子，触发 AI 画O)")
        print("3. 发送 /reset (机器人拿橡皮并擦除棋盘)")
        print("4. 发送 /drop_pen (机器人放笔)")
        print("q. 退出")
        
        choice = input("请输入 1/2/3/4/q: ").strip().lower()
        
        if choice == '1':
            test_start_game()
        elif choice == '2':
            test_send_vision_state()
        elif choice == '3':
            test_reset_game()
        elif choice == '4':
            test_drop_pen()
        elif choice == 'q':
            print("退出测试。")
            break
        else:
            print("无效输入，请重新输入。")
