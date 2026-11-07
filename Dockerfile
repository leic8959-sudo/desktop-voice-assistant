FROM python:3.12-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

WORKDIR /app
COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt
COPY . .

# 配置通过挂载文件或环境变量注入，镜像不包含本地运行状态。
EXPOSE 8765
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s CMD python tools/healthcheck.py
CMD ["python", "gateway.py", "--config", "/app/config.local.json"]
