# VibeKey 后端镜像：前端构建 + 后端运行（单容器）
#
# 用法：
#   docker build -t vibekey .
#   docker run -p 8080:8080 -e JWT_SECRET=<随机串> vibekey
#
# 多阶段：阶段 1 构建前端 dist，阶段 2 跑后端 serve dist + API + WebSocket 桥。

# ---- 阶段 1：构建前端 ----
FROM node:22-alpine AS frontend
WORKDIR /app
COPY package.json package-lock.json* ./
RUN npm ci --no-audit --no-fund
COPY . .
RUN npm run build

# ---- 阶段 2：后端运行 ----
FROM node:22-alpine
ENV NODE_ENV=production
WORKDIR /app

# 后端依赖（只装生产依赖）
COPY server/package.json server/package-lock.json* ./server/
RUN cd server && npm ci --omit=dev --no-audit --no-fund

# 后端代码
COPY server ./server

# 前端构建产物（阶段 1 的 dist）
COPY --from=frontend /app/dist ./dist

ENV PORT=8080 \
    STATIC_DIR=/app/dist \
    DB_PATH=/app/data/vibekey.db

# 数据卷（SQLite 持久化）
VOLUME /app/data

EXPOSE 8080

CMD ["node", "server/index.js"]
