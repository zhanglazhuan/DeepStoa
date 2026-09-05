# DeepStoa

## 总体介绍
DeepStoa 是一款墨水屏手机形态的设备，主要面向中小学生教育市场，提供护眼、专注、便捷的学习体验.
设备和系统都是开源的，允许用户自己 DIY 软件和固件. 在 AI coding 如此强大的今天，用户完全可以把 DeepStoa 作为个性化的工具.

设备的配置如下：
- 屏幕：3.97英寸 800x480 分辨率墨水屏 + 双点触控
- 处理器：ESP32S3
- 系统：Zephyr OS

支持的功能如下：
- 阅读
- 卡片学习
- 时间管理

## 项目介绍
本项目是 DeepStoa 的官网，用户可以在 Web 上查看产品介绍、下单购买、学习使用案例.
项目的网站前期深度参考(甚至可以说是 copy): https://remarkable.com, 如果你需要打开则使用 VPN 代理端口 6789
前后端代码放在一个仓库里。后端代码使用 python Django 框架. 前端使用 React.

## 本地运行

### 环境要求

- Python 3.12+
- Node.js 20+

### 1. 克隆并进入项目

```bash
git clone <repo-url> && cd deepstoa
```

### 2. 启动后端

```bash
cd backend

# 创建虚拟环境（首次）
python -m venv venv

# 激活虚拟环境
# Windows:
venv\Scripts\activate
# macOS / Linux:
source venv/bin/activate

# 安装依赖
pip install -r requirements.txt

# 数据库迁移
python manage.py migrate

# 启动 Django 开发服务器（默认 8000 端口）
python manage.py runserver
```

### 3. 启动前端

打开新终端：

```bash
cd frontend

# 安装依赖（首次）
npm install

# 启动 Vite 开发服务器（默认 5173 端口）
npm run dev
```

访问 http://localhost:5173 即可看到网站。

### 4. 配置 Google 登录（可选）

如需启用 Google OAuth 登录功能：

1. 前往 [Google Cloud Console](https://console.cloud.google.com/apis/credentials)
2. 创建 OAuth 2.0 客户端 ID（Web 应用类型）
3. 添加已授权的重定向 URI：
   ```
   http://localhost:5173/_allauth/browser/v1/auth/provider/callback
   ```
4. 复制 `backend/.env.example` 为 `backend/.env`，填入 Google 凭据：

   ```bash
   cp backend/.env.example backend/.env
   ```

   编辑 `backend/.env`：
   ```ini
   GOOGLE_CLIENT_ID=your-client-id.apps.googleusercontent.com
   GOOGLE_CLIENT_SECRET=your-client-secret
   ```

5. 重启后端服务器即可

不配置 Google 登录时，网站其余功能（产品浏览、使用案例、关于页面等）均可正常访问。Google 登录仅影响预购下单和账号页面。

## 生产部署安全

项目通过 `DJANGO_ENV` 环境变量区分开发/生产模式。设置 `DJANGO_ENV=production` 后自动启用以下安全措施：

| 措施 | 开发模式 | 生产模式 |
|---|---|---|
| DEBUG | 开启 | 关闭 |
| ALLOWED_HOSTS | `*` | 由 `DJANGO_ALLOWED_HOSTS` 环境变量指定 |
| CORS | 允许所有域名 | 由 `CORS_ALLOWED_ORIGINS` 环境变量指定 |
| HTTPS | 不强制 | 开启（HSTS / Secure Cookie / SSL 重定向） |
| 速率限制 | 匿名 20次/分，登录 60次/分 | 匿名 20次/分，登录 60次/分 |
| CSRF | 开发用宽松策略 | 仅信任 `DJANGO_CSRF_TRUSTED_ORIGINS` 指定的域名 |
| 数据库 | SQLite | 支持 PostgreSQL（设置 `DATABASE_URL`） |

生产环境部署前，请确保：
- `DJANGO_SECRET_KEY` 使用足够长的随机字符串（`openssl rand -base64 64`）
- `.env` 文件**绝不**提交到 Git（已在 `.gitignore` 中排除）
- 所有凭据通过环境变量或 `.env` 注入，无硬编码默认值

### 项目结构

```
deepstoa/
├── backend/          # Django 后端
│   ├── auth_api/     # 用户信息 API
│   ├── config/       # Django 项目配置
│   ├── orders/       # 订单系统
│   └── waitlist/     # 预约列表
├── frontend/         # React 前端
│   └── src/
│       ├── components/  # 可复用组件
│       ├── contexts/    # React Context (Auth)
│       └── pages/       # 页面组件
└── docs/             # 设计文档和计划
```
