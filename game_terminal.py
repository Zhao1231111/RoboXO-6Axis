import requests
import argparse
import sys

def print_menu():
    print("\n" + "="*40)
    print("🤖 井字棋游戏控制终端 (手动反馈工具)")
    print("="*40)
    print("  1. 开始游戏 (触发抓笔) -> POST /api/game/start")
    print("  2. 机器人已抓起笔 (反馈) -> POST /api/game/robot/pen_ready")
    print("  3. 机器人完成画图动作 (反馈) -> POST /api/game/robot/action_done")
    print("  4. 放下笔 -> POST /api/game/drop_pen")
    print("  5. 重置游戏 (触发擦黑板) -> POST /api/game/reset")
    print("  6. 手动下棋 (指定位置画 X 或 O) -> POST /api/game/manual_draw")
    print("  0. 退出本控制台")
    print("="*40)

def send_post(url, json_data=None):
    try:
        if json_data:
            res = requests.post(url, json=json_data, timeout=2.0)
        else:
            res = requests.post(url, timeout=2.0)
            
        print(f"\n[服务器响应 {res.status_code}]:")
        try:
            print(res.json())
        except:
            print(res.text)
    except Exception as e:
        print(f"\n[错误] 请求失败: {e}")

def main():
    parser = argparse.ArgumentParser(description="游戏状态机终端控制")
    parser.add_argument("--gateway", type=str, default="http://127.0.0.1:8000", help="网关地址 (默认: http://127.0.0.1:8000)")
    args = parser.parse_args()
    
    gateway_url = args.gateway.rstrip('/')

    while True:
        print_menu()
        choice = input("请输入选项并回车: ").strip()
        
        if choice == '1':
            data = {"difficulty": "hard", "first_player": "human"}
            send_post(f"{gateway_url}/api/game/start", json_data=data)
        elif choice == '2':
            send_post(f"{gateway_url}/api/game/robot/pen_ready")
        elif choice == '3':
            send_post(f"{gateway_url}/api/game/robot/action_done")
        elif choice == '4':
            send_post(f"{gateway_url}/api/game/drop_pen")
        elif choice == '5':
            send_post(f"{gateway_url}/api/game/reset")
        elif choice == '6':
            shape = input("请输入要画的形状 (x 或 o): ").strip().lower()
            if shape not in ['x', 'o']:
                print("形状只能是 x 或 o！")
                continue
            pos_str = input("请输入位置编号 (0-8): ").strip()
            if not pos_str.isdigit() or not (0 <= int(pos_str) <= 8):
                print("位置只能是 0 到 8 之间的整数！")
                continue
            data = {"shape": shape, "position": int(pos_str)}
            send_post(f"{gateway_url}/api/game/manual_draw", json_data=data)
        elif choice == '0':
            print("退出终端控制。")
            sys.exit(0)
        else:
            print("无效的选项，请重新输入。")

if __name__ == "__main__":
    main()
