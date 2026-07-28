FROM python:3.12-slim

WORKDIR /app

# Install deps first for better layer caching
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY collector.py .

# Unbuffered stdout so logs stream to `docker logs`
ENV PYTHONUNBUFFERED=1

CMD ["python", "collector.py"]
