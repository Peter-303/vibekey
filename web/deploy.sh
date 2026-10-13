#!/usr/bin/env bash
# VibeKey 云端一键部署脚本（在你的服务器上跑）
#
# 用法：
#   1. 把你的主域名解析到本服务器（子域名加 A 记录指向本机公网 IP）
#   2. chmod +x deploy.sh && ./deploy.sh vibekey.你的域名.com
#
# 脚本会：
#   - 检查 Docker / Docker Compose
#   - 生成强随机 JWT_SECRET（如果没设）
#   - docker compose up -d --build
#   - 等 Caddy 签好 SSL 证书
#   - 打印访问地址

set -euo pipefail

DOMAIN="${1:-}"
if [[ -z "$DOMAIN" ]]; then
    echo "用法: $0 <域名>"
    echo "示例: $0 vibekey.bealux.tech"
    exit 1
fi

echo "=== VibeKey 云端部署 ==="
echo "域名: $DOMAIN"

# 检查 Docker
if ! command -v docker >/dev/null 2>&1; then
    echo "错误: 未安装 Docker。请先装 Docker："
    echo "  curl -fsSL https://get.docker.com | sh"
    exit 1
fi
if ! docker compose version >/dev/null 2>&1; then
    echo "错误: 未安装 Docker Compose 插件。"
    exit 1
fi

# 生成 JWT_SECRET（如果没设）
if [[ -z "${JWT_SECRET:-}" ]]; then
    JWT_SECRET="$(head -c 32 /dev/urandom | base64 | tr -d '=+/')"
    echo "已生成 JWT_SECRET（请保存，重启后沿用）:"
    echo "  $JWT_SECRET"
    echo ""
    echo "下次部署请用: JWT_SECRET=$JWT_SECRET $0 $DOMAIN"
fi

export DOMAIN JWT_SECRET

echo ""
echo "=== 构建并启动 ==="
docker compose up -d --build

echo ""
echo "=== 等待服务就绪 ==="
sleep 5
docker compose ps

echo ""
echo "=== 完成 ==="
echo "访问: https://$DOMAIN"
echo "后端: docker compose logs -f backend"
echo "Caddy: docker compose logs -f caddy"
echo ""
echo "首次访问 Caddy 会自动签 Let's Encrypt 证书（约 30 秒）。"
echo "如果 1 分钟后还打不开，检查："
echo "  1. DNS 是否解析到本机（dig +short $DOMAIN）"
echo "  2. 防火墙是否放行 80/443（云厂商安全组 + ufw）"
echo "  3. docker compose logs caddy"
