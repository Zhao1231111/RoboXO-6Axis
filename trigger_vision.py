import requests
import argparse
import sys
import time

def trigger_real_vision_pc(ip: str, port: int):
    """
    触发真实的视觉 PC 进行识别。
    """
    url = f"http://{ip}:{port}/api/vision/recognize"
    print(f"[*] 正在向视觉 PC 发送触发命令: {url}")
    
    try:
        start_time = time.time()
        response = requests.post(url, timeout=5.0)
        
        if response.status_code == 200:
            print(f"[+] 触发成功! (耗时: {time.time() - start_time:.2f}s)")
            print(f"[+] 响应内容: {response.text}")
            print("[*] 视觉 PC 现在应该会在 recognition_delay_sec 后向 Gateway 发送棋盘状态。")
        else:
            print(f"[-] 触发失败，状态码: {response.status_code}")
            print(f"[-] 响应内容: {response.text}")
            
    except requests.exceptions.Timeout:
        print("[-] 请求超时！请检查视觉 PC 是否开启并监听该端口。")
    except requests.exceptions.ConnectionError:
        print("[-] 连接被拒绝！请检查网络连接或视觉 PC 的 IP/端口。")
    except Exception as e:
        print(f"[-] 发生未知错误: {e}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="触发视觉 PC 的识别流程测试脚本")
    parser.add_argument("--ip", type=str, default="10.192.23.74", help="视觉 PC 的 IP 地址 (默认: 10.192.23.74)")
    parser.add_argument("--port", type=int, default=8765, help="视觉 PC 监听的端口 (默认: 8765)")
    
    args = parser.parse_args()
    trigger_real_vision_pc(args.ip, args.port)
