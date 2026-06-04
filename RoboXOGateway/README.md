# RoboXO Gateway

Installation:

```
$ python3 -m venv .venv
$ source .venv/bin/activate
$ pip install -r requirements.txt
```

Run:

```
$ sudo .venv/bin/uvicorn app.main:app --host 0.0.0.0 --port 8000
```

Open http://127.0.0.1:8000
