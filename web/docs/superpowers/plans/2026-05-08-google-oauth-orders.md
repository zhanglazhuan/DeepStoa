# Google OAuth Login & Order System — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Google OAuth login, user profile page, and order creation/tracking system

**Architecture:** django-allauth headless handles Google OAuth via REST endpoints proxied through Vite. Django sessions for auth state. DRF views for orders CRUD. React Context for client-side auth state, ProtectedRoute wrapper for gated pages.

**Tech Stack:** Django 5.2, DRF 3.17, django-allauth 65+ (headless), React 19, React Router 7, Tailwind CSS 4

**Prerequisite (manual):** Create a Google Cloud Console OAuth 2.0 Client ID. Set authorized redirect URI to `http://localhost:5173/_allauth/browser/v1/auth/provider/callback`. Obtain Client ID and Secret.

---

### Task 1: Install django-allauth with headless support

**Files:**
- Modify: `backend/requirements.txt`

- [ ] **Step 1: Add allauth to requirements**

```txt
Django>=5.0,<6.0
djangorestframework>=3.15,<4.0
django-cors-headers>=4.0,<5.0
django-allauth[headless]>=65.0,<66.0
```

- [ ] **Step 2: Install the package**

```bash
cd backend && pip install "django-allauth[headless]>=65.0,<66.0"
```

Expected: Package installs successfully, no errors.

- [ ] **Step 3: Commit**

```bash
git add backend/requirements.txt
git commit -m "deps: add django-allauth with headless support"
```

---

### Task 2: Configure Django settings for allauth and Google OAuth

**Files:**
- Modify: `backend/config/settings.py`

- [ ] **Step 1: Update INSTALLED_APPS and add allauth config**

Open `backend/config/settings.py`. Add `allauth` apps to `INSTALLED_APPS`:

```python
INSTALLED_APPS = [
    'django.contrib.admin',
    'django.contrib.auth',
    'django.contrib.contenttypes',
    'django.contrib.sessions',
    'django.contrib.messages',
    'django.contrib.staticfiles',
    'rest_framework',
    'corsheaders',
    'allauth',
    'allauth.account',
    'allauth.headless',
    'allauth.socialaccount',
    'allauth.socialaccount.providers.google',
    'waitlist',
]
```

- [ ] **Step 2: Add authentication backends**

After `MIDDLEWARE`, append:

```python
AUTHENTICATION_BACKENDS = [
    'django.contrib.auth.backends.ModelBackend',
    'allauth.account.auth_backends.AuthenticationBackend',
]
```

- [ ] **Step 3: Add allauth-specific settings**

At the end of the file, append:

```python
# Allauth
SOCIALACCOUNT_PROVIDERS = {
    'google': {
        'APP': {
            'client_id': os.environ.get('GOOGLE_CLIENT_ID', ''),
            'secret': os.environ.get('GOOGLE_CLIENT_SECRET', ''),
        },
        'SCOPE': ['profile', 'email'],
        'AUTH_PARAMS': {'access_type': 'online'},
    }
}
SOCIALACCOUNT_EMAIL_AUTHENTICATION = True
ACCOUNT_EMAIL_VERIFICATION = 'none'
HEADLESS_ONLY = True
```

- [ ] **Step 4: Commit**

```bash
git add backend/config/settings.py
git commit -m "feat: configure allauth and Google OAuth provider"
```

---

### Task 3: Mount allauth URLs and create user-info API endpoint

**Files:**
- Create: `backend/auth_api/__init__.py`
- Create: `backend/auth_api/urls.py`
- Create: `backend/auth_api/views.py`
- Modify: `backend/config/settings.py` (add `auth_api` to INSTALLED_APPS)
- Modify: `backend/config/urls.py`

- [ ] **Step 1: Create the auth_api app directory and files**

```bash
mkdir -p backend/auth_api
```

Create `backend/auth_api/__init__.py` (empty).

- [ ] **Step 2: Create the user-info view**

Write `backend/auth_api/views.py`:

```python
from rest_framework.decorators import api_view
from rest_framework.response import Response

@api_view(['GET'])
def user_info(request):
    user = request.user
    if not user.is_authenticated:
        return Response({'is_authenticated': False}, status=401)
    return Response({
        'id': user.id,
        'email': user.email,
        'first_name': user.first_name,
        'last_name': user.last_name,
        'is_authenticated': True,
    })
```

- [ ] **Step 3: Create auth_api URL conf**

Write `backend/auth_api/urls.py`:

```python
from django.urls import path
from . import views

urlpatterns = [
    path('user/', views.user_info, name='auth-user'),
]
```

- [ ] **Step 4: Add auth_api to INSTALLED_APPS in settings.py**

In `backend/config/settings.py`, add `'auth_api',` to `INSTALLED_APPS` (right after `'waitlist',`).

- [ ] **Step 5: Wire URLs in root URL conf**

Edit `backend/config/urls.py`:

```python
from django.contrib import admin
from django.urls import path, include

urlpatterns = [
    path('admin/', admin.site.urls),
    path('api/', include('waitlist.urls')),
    path('api/auth/', include('auth_api.urls')),
    path('_allauth/', include('allauth.headless.urls')),
]
```

- [ ] **Step 6: Run migrations for allauth SocialAccount models**

```bash
cd backend && python manage.py migrate
```

Expected: Allauth migrations run successfully.

- [ ] **Step 7: Commit**

```bash
git add backend/auth_api/ backend/config/settings.py backend/config/urls.py
git commit -m "feat: add user-info API endpoint and mount allauth headless URLs"
```

---

### Task 4: Update Vite proxy for allauth endpoints

**Files:**
- Modify: `frontend/vite.config.ts`

- [ ] **Step 1: Add _allauth proxy rule**

Change the proxy config in `frontend/vite.config.ts`:

```typescript
import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'

export default defineConfig({
  plugins: [react(), tailwindcss()],
  server: {
    proxy: {
      '/api': 'http://localhost:8000',
      '/_allauth': 'http://localhost:8000',
    },
  },
})
```

- [ ] **Step 2: Commit**

```bash
git add frontend/vite.config.ts
git commit -m "feat: proxy _allauth paths to Django backend"
```

---

### Task 5: Create AuthContext and ProtectedRoute

**Files:**
- Create: `frontend/src/contexts/AuthContext.tsx`
- Create: `frontend/src/components/ProtectedRoute.tsx`

- [ ] **Step 1: Create AuthContext**

Write `frontend/src/contexts/AuthContext.tsx`:

```typescript
import { createContext, useContext, useState, useEffect, useCallback, type ReactNode } from 'react'

export interface User {
  id: number
  email: string
  first_name: string
  last_name: string
}

interface AuthState {
  user: User | null
  loading: boolean
  login: () => void
  logout: () => Promise<void>
}

const AuthContext = createContext<AuthState | undefined>(undefined)

export function AuthProvider({ children }: { children: ReactNode }) {
  const [user, setUser] = useState<User | null>(null)
  const [loading, setLoading] = useState(true)

  useEffect(() => {
    fetch('/api/auth/user/')
      .then((res) => (res.ok ? res.json() : null))
      .then((data) => setUser(data))
      .finally(() => setLoading(false))
  }, [])

  const login = useCallback(() => {
    fetch('/_allauth/browser/v1/auth/provider/redirect', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        provider: 'google',
        callback_url: window.location.origin + '/auth/callback',
        process: 'login',
      }),
    })
      .then((res) => res.json())
      .then((data) => {
        if (data.data?.url) {
          window.location.href = data.data.url
        }
      })
  }, [])

  const logout = useCallback(async () => {
    await fetch('/_allauth/browser/v1/auth/session', { method: 'DELETE' })
    setUser(null)
  }, [])

  return (
    <AuthContext.Provider value={{ user, loading, login, logout }}>
      {children}
    </AuthContext.Provider>
  )
}

export function useAuth() {
  const ctx = useContext(AuthContext)
  if (!ctx) throw new Error('useAuth must be used within AuthProvider')
  return ctx
}
```

- [ ] **Step 2: Create ProtectedRoute**

Write `frontend/src/components/ProtectedRoute.tsx`:

```typescript
import { Navigate } from 'react-router-dom'
import { useAuth } from '../contexts/AuthContext'

export default function ProtectedRoute({ children }: { children: React.ReactNode }) {
  const { user, loading } = useAuth()

  if (loading) return null
  if (!user) return <Navigate to="/" replace />
  return <>{children}</>
}
```

- [ ] **Step 3: Commit**

```bash
git add frontend/src/contexts/AuthContext.tsx frontend/src/components/ProtectedRoute.tsx
git commit -m "feat: add AuthContext and ProtectedRoute"
```

---

### Task 6: Create AuthCallback page

**Files:**
- Create: `frontend/src/pages/AuthCallback.tsx`

- [ ] **Step 1: Write AuthCallback page**

Write `frontend/src/pages/AuthCallback.tsx`:

```typescript
import { useEffect, useState } from 'react'
import { useNavigate, useSearchParams } from 'react-router-dom'

export default function AuthCallback() {
  const navigate = useNavigate()
  const [searchParams] = useSearchParams()
  const [error, setError] = useState(false)

  useEffect(() => {
    const errorParam = searchParams.get('error')
    if (errorParam) {
      setError(true)
      return
    }

    fetch('/_allauth/browser/v1/auth/session')
      .then((res) => res.json())
      .then((data) => {
        if (data.data?.user) {
          navigate('/', { replace: true })
        } else {
          setError(true)
        }
      })
      .catch(() => setError(true))
  }, [navigate, searchParams])

  if (error) {
    return (
      <div className="flex min-h-[60vh] items-center justify-center">
        <div className="text-center">
          <p className="text-lg text-neutral-900">Sign in failed</p>
          <p className="mt-2 text-sm text-neutral-500">
            The sign-in process was cancelled or encountered an error.
          </p>
          <a href="/" className="mt-4 inline-block text-sm text-neutral-900 underline">
            Return home
          </a>
        </div>
      </div>
    )
  }

  return (
    <div className="flex min-h-[60vh] items-center justify-center">
      <p className="text-neutral-500">Signing you in...</p>
    </div>
  )
}
```

- [ ] **Step 2: Commit**

```bash
git add frontend/src/pages/AuthCallback.tsx
git commit -m "feat: add OAuth callback handler page"
```

---

### Task 7: Wire AuthProvider and new routes into App.tsx

**Files:**
- Modify: `frontend/src/App.tsx`

- [ ] **Step 1: Update App.tsx**

Replace `frontend/src/App.tsx` with:

```typescript
import { Routes, Route } from 'react-router-dom'
import { AuthProvider } from './contexts/AuthContext'
import Nav from './components/Nav'
import HomePage from './pages/HomePage'
import ProductPage from './pages/ProductPage'
import UseCasesPage from './pages/UseCasesPage'
import AboutPage from './pages/AboutPage'
import PreOrderPage from './pages/PreOrderPage'
import AuthCallback from './pages/AuthCallback'

export default function App() {
  return (
    <AuthProvider>
      <Nav />
      <main>
        <Routes>
          <Route path="/" element={<HomePage />} />
          <Route path="/product" element={<ProductPage />} />
          <Route path="/use-cases" element={<UseCasesPage />} />
          <Route path="/about" element={<AboutPage />} />
          <Route path="/pre-order" element={<PreOrderPage />} />
          <Route path="/auth/callback" element={<AuthCallback />} />
        </Routes>
      </main>
    </AuthProvider>
  )
}
```

- [ ] **Step 2: Commit**

```bash
git add frontend/src/App.tsx
git commit -m "feat: integrate AuthProvider and AuthCallback route"
```

---

### Task 8: Update Nav with sign-in button and user dropdown

**Files:**
- Modify: `frontend/src/components/Nav.tsx`

- [ ] **Step 1: Rewrite Nav with auth-aware UI**

Replace `frontend/src/components/Nav.tsx` with:

```typescript
import { useState, useRef, useEffect } from 'react'
import { Link, useLocation } from 'react-router-dom'
import { useAuth } from '../contexts/AuthContext'

const links = [
  { to: '/product', label: 'Product' },
  { to: '/use-cases', label: 'Use Cases' },
  { to: '/about', label: 'About' },
]

export default function Nav() {
  const { pathname } = useLocation()
  const { user, loading, login, logout } = useAuth()
  const [menuOpen, setMenuOpen] = useState(false)
  const menuRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    function handleClick(e: MouseEvent) {
      if (menuRef.current && !menuRef.current.contains(e.target as Node)) {
        setMenuOpen(false)
      }
    }
    document.addEventListener('mousedown', handleClick)
    return () => document.removeEventListener('mousedown', handleClick)
  }, [])

  return (
    <nav className="sticky top-0 z-50 bg-paper/80 backdrop-blur-sm">
      <div className="mx-auto flex max-w-7xl items-center justify-between px-6 py-5 lg:px-12">
        <Link to="/" className="text-xl font-semibold tracking-tight text-neutral-900">
          DeepStoa
        </Link>
        <div className="flex items-center gap-8">
          {links.map((l) => (
            <Link
              key={l.to}
              to={l.to}
              className={`text-sm transition-colors duration-200 hover:text-neutral-900 ${
                pathname === l.to ? 'text-neutral-900' : 'text-neutral-500'
              }`}
            >
              {l.label}
            </Link>
          ))}
          <a
            href="https://github.com/deepstoa/epos"
            target="_blank"
            rel="noopener noreferrer"
            aria-label="GitHub"
            className="text-neutral-500 transition-colors duration-200 hover:text-neutral-900"
          >
            <svg className="h-5 w-5" viewBox="0 0 24 24" fill="currentColor">
              <path d="M12 0C5.37 0 0 5.37 0 12c0 5.31 3.435 9.795 8.205 11.385.6.105.825-.255.825-.57 0-.285-.015-1.23-.015-2.235-3.015.555-3.795-.735-4.035-1.41-.135-.345-.72-1.41-1.23-1.695-.42-.225-1.02-.78-.015-.795.945-.015 1.62.87 1.845 1.23 1.08 1.815 2.805 1.305 3.495.99.105-.78.42-1.305.765-1.605-2.67-.3-5.46-1.335-5.46-5.925 0-1.305.465-2.385 1.23-3.225-.12-.3-.54-1.53.12-3.18 0 0 1.005-.315 3.3 1.23.96-.27 1.98-.405 3-.405s2.04.135 3 .405c2.295-1.56 3.3-1.23 3.3-1.23.66 1.65.24 2.88.12 3.18.765.84 1.23 1.905 1.23 3.225 0 4.605-2.805 5.625-5.475 5.925.435.375.81 1.095.81 2.22 0 1.605-.015 2.895-.015 3.3 0 .315.225.69.825.57A12.02 12.02 0 0024 12c0-6.63-5.37-12-12-12z" />
            </svg>
          </a>

          {loading ? null : user ? (
            <div className="relative" ref={menuRef}>
              <button
                onClick={() => setMenuOpen(!menuOpen)}
                className="flex items-center gap-2 text-sm text-neutral-700 hover:text-neutral-900 transition-colors"
              >
                <span className="flex h-8 w-8 items-center justify-center rounded-full bg-neutral-200 text-xs font-medium text-neutral-600">
                  {user.first_name?.[0]}{user.last_name?.[0]}
                </span>
              </button>
              {menuOpen && (
                <div className="absolute right-0 top-full mt-2 w-48 rounded-lg border bg-white py-1 shadow-lg">
                  <div className="border-b px-4 py-2">
                    <p className="text-sm font-medium text-neutral-900 truncate">
                      {user.first_name} {user.last_name}
                    </p>
                    <p className="text-xs text-neutral-500 truncate">{user.email}</p>
                  </div>
                  <Link
                    to="/account"
                    onClick={() => setMenuOpen(false)}
                    className="block px-4 py-2 text-sm text-neutral-700 hover:bg-neutral-50"
                  >
                    Account
                  </Link>
                  <button
                    onClick={() => { logout(); setMenuOpen(false) }}
                    className="block w-full px-4 py-2 text-left text-sm text-neutral-700 hover:bg-neutral-50"
                  >
                    Sign out
                  </button>
                </div>
              )}
            </div>
          ) : (
            <button
              onClick={login}
              className="text-sm text-neutral-500 transition-colors duration-200 hover:text-neutral-900"
            >
              Sign in
            </button>
          )}

          <Link
            to="/pre-order"
            className="rounded-full bg-neutral-900 px-5 py-2 text-sm font-medium text-white transition-colors hover:bg-neutral-700"
          >
            Pre-order
          </Link>
        </div>
      </div>
    </nav>
  )
}
```

- [ ] **Step 2: Commit**

```bash
git add frontend/src/components/Nav.tsx
git commit -m "feat: add sign-in button and user dropdown to Nav"
```

---

### Task 9: Create the orders Django app with model

**Files:**
- Create: `backend/orders/__init__.py`
- Create: `backend/orders/models.py`
- Create: `backend/orders/admin.py`
- Modify: `backend/config/settings.py` (add `orders` to INSTALLED_APPS)

- [ ] **Step 1: Create orders app directory**

```bash
mkdir -p backend/orders
```

Create `backend/orders/__init__.py` (empty).

- [ ] **Step 2: Write Order model**

Write `backend/orders/models.py`:

```python
from django.db import models
from django.contrib.auth.models import User


class Order(models.Model):
    class Status(models.TextChoices):
        PENDING = 'pending', 'Pending'
        CONFIRMED = 'confirmed', 'Confirmed'
        PRODUCING = 'producing', 'In Production'
        SHIPPED = 'shipped', 'Shipped'
        CANCELLED = 'cancelled', 'Cancelled'

    user = models.ForeignKey(User, on_delete=models.CASCADE, related_name='orders')
    quantity = models.PositiveSmallIntegerField(default=1)
    address = models.TextField()
    note = models.TextField(blank=True)
    status = models.CharField(max_length=20, choices=Status.choices, default=Status.PENDING)
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        ordering = ['-created_at']

    def __str__(self):
        return f'Order #{self.id} — {self.user.email}'
```

- [ ] **Step 3: Register in admin**

Write `backend/orders/admin.py`:

```python
from django.contrib import admin
from .models import Order

@admin.register(Order)
class OrderAdmin(admin.ModelAdmin):
    list_display = ['id', 'user', 'status', 'quantity', 'created_at']
    list_filter = ['status']
    search_fields = ['user__email', 'user__first_name']
```

- [ ] **Step 4: Add orders to INSTALLED_APPS**

In `backend/config/settings.py`, add `'orders',` to `INSTALLED_APPS`.

- [ ] **Step 5: Create and run migrations**

```bash
cd backend && python manage.py makemigrations orders && python manage.py migrate
```

Expected: `orders/migrations/0001_initial.py` created, migrations applied.

- [ ] **Step 6: Commit**

```bash
git add backend/orders/ backend/config/settings.py
git commit -m "feat: add Order model and orders app"
```

---

### Task 10: Create Order serializers and API views

**Files:**
- Create: `backend/orders/serializers.py`
- Create: `backend/orders/views.py`
- Create: `backend/orders/urls.py`
- Modify: `backend/config/urls.py`

- [ ] **Step 1: Write serializers**

Write `backend/orders/serializers.py`:

```python
from rest_framework import serializers
from .models import Order


class OrderListSerializer(serializers.ModelSerializer):
    class Meta:
        model = Order
        fields = ['id', 'status', 'quantity', 'created_at']


class OrderCreateSerializer(serializers.ModelSerializer):
    class Meta:
        model = Order
        fields = ['quantity', 'address', 'note']


class OrderDetailSerializer(serializers.ModelSerializer):
    class Meta:
        model = Order
        fields = ['id', 'status', 'quantity', 'address', 'note', 'created_at', 'updated_at']
        read_only_fields = ['id', 'status', 'created_at', 'updated_at']
```

- [ ] **Step 2: Write views**

Write `backend/orders/views.py`:

```python
from rest_framework import generics, permissions
from rest_framework.exceptions import PermissionDenied
from .models import Order
from .serializers import OrderListSerializer, OrderCreateSerializer, OrderDetailSerializer


class IsOwner(permissions.BasePermission):
    def has_object_permission(self, request, view, obj):
        return obj.user == request.user


class OrderListCreateView(generics.ListCreateAPIView):
    permission_classes = [permissions.IsAuthenticated]

    def get_serializer_class(self):
        if self.request.method == 'POST':
            return OrderCreateSerializer
        return OrderListSerializer

    def get_queryset(self):
        return Order.objects.filter(user=self.request.user)

    def perform_create(self, serializer):
        serializer.save(user=self.request.user)


class OrderDetailView(generics.RetrieveAPIView):
    serializer_class = OrderDetailSerializer
    permission_classes = [permissions.IsAuthenticated, IsOwner]

    def get_queryset(self):
        return Order.objects.filter(user=self.request.user)
```

- [ ] **Step 3: Write URL conf**

Write `backend/orders/urls.py`:

```python
from django.urls import path
from . import views

urlpatterns = [
    path('orders/', views.OrderListCreateView.as_view(), name='order-list-create'),
    path('orders/<int:pk>/', views.OrderDetailView.as_view(), name='order-detail'),
]
```

- [ ] **Step 4: Wire orders URLs into root**

Edit `backend/config/urls.py`, add the orders include:

```python
from django.contrib import admin
from django.urls import path, include

urlpatterns = [
    path('admin/', admin.site.urls),
    path('api/', include('waitlist.urls')),
    path('api/', include('orders.urls')),
    path('api/auth/', include('auth_api.urls')),
    path('_allauth/', include('allauth.headless.urls')),
]
```

- [ ] **Step 5: Commit**

```bash
git add backend/orders/serializers.py backend/orders/views.py backend/orders/urls.py backend/config/urls.py
git commit -m "feat: add Order serializers, views, and API endpoints"
```

---

### Task 11: Create AccountPage

**Files:**
- Create: `frontend/src/pages/AccountPage.tsx`

- [ ] **Step 1: Write AccountPage**

Write `frontend/src/pages/AccountPage.tsx`:

```typescript
import { useState, useEffect } from 'react'
import { Link } from 'react-router-dom'
import { useAuth } from '../contexts/AuthContext'

interface Order {
  id: number
  status: string
  quantity: number
  created_at: string
}

const statusColors: Record<string, string> = {
  pending: 'bg-yellow-100 text-yellow-800',
  confirmed: 'bg-blue-100 text-blue-800',
  producing: 'bg-purple-100 text-purple-800',
  shipped: 'bg-green-100 text-green-800',
  cancelled: 'bg-red-100 text-red-800',
}

export default function AccountPage() {
  const { user } = useAuth()
  const [orders, setOrders] = useState<Order[]>([])
  const [loading, setLoading] = useState(true)

  useEffect(() => {
    fetch('/api/orders/')
      .then((res) => res.json())
      .then(setOrders)
      .finally(() => setLoading(false))
  }, [])

  return (
    <div className="mx-auto max-w-3xl px-6 py-16 lg:px-12">
      <div className="mb-12">
        <div className="flex items-center gap-4">
          <div className="flex h-16 w-16 items-center justify-center rounded-full bg-neutral-200 text-xl font-medium text-neutral-600">
            {user?.first_name?.[0]}{user?.last_name?.[0]}
          </div>
          <div>
            <h1 className="text-2xl font-semibold text-neutral-900">
              {user?.first_name} {user?.last_name}
            </h1>
            <p className="text-neutral-500">{user?.email}</p>
          </div>
        </div>
      </div>

      <h2 className="mb-6 text-xl font-semibold text-neutral-900">Your Orders</h2>

      {loading ? (
        <p className="text-neutral-500">Loading...</p>
      ) : orders.length === 0 ? (
        <div className="rounded-lg border border-dashed border-neutral-300 px-6 py-12 text-center">
          <p className="text-neutral-500">No orders yet.</p>
          <Link
            to="/pre-order"
            className="mt-3 inline-block text-sm font-medium text-neutral-900 underline"
          >
            Place your first pre-order
          </Link>
        </div>
      ) : (
        <div className="space-y-3">
          {orders.map((order) => (
            <Link
              key={order.id}
              to={`/account/orders/${order.id}`}
              className="block rounded-lg border border-neutral-200 p-4 transition-colors hover:border-neutral-400"
            >
              <div className="flex items-center justify-between">
                <div>
                  <p className="font-medium text-neutral-900">Order #{order.id}</p>
                  <p className="text-sm text-neutral-500">
                    {new Date(order.created_at).toLocaleDateString()}
                  </p>
                </div>
                <div className="text-right">
                  <span
                    className={`inline-block rounded-full px-3 py-1 text-xs font-medium ${
                      statusColors[order.status] || ''
                    }`}
                  >
                    {order.status.charAt(0).toUpperCase() + order.status.slice(1)}
                  </span>
                  <p className="mt-1 text-sm text-neutral-500">Qty: {order.quantity}</p>
                </div>
              </div>
            </Link>
          ))}
        </div>
      )}
    </div>
  )
}
```

- [ ] **Step 2: Commit**

```bash
git add frontend/src/pages/AccountPage.tsx
git commit -m "feat: add AccountPage with profile and order list"
```

---

### Task 12: Create OrderDetailPage

**Files:**
- Create: `frontend/src/pages/OrderDetailPage.tsx`

- [ ] **Step 1: Write OrderDetailPage**

Write `frontend/src/pages/OrderDetailPage.tsx`:

```typescript
import { useState, useEffect } from 'react'
import { useParams, Link } from 'react-router-dom'

interface OrderDetail {
  id: number
  status: string
  quantity: number
  address: string
  note: string
  created_at: string
  updated_at: string
}

const statusSteps = [
  { key: 'pending', label: 'Order placed' },
  { key: 'confirmed', label: 'Confirmed' },
  { key: 'producing', label: 'In production' },
  { key: 'shipped', label: 'Shipped' },
]

const statusIndex: Record<string, number> = {
  pending: 0,
  confirmed: 1,
  producing: 2,
  shipped: 3,
}

export default function OrderDetailPage() {
  const { id } = useParams<{ id: string }>()
  const [order, setOrder] = useState<OrderDetail | null>(null)
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState(false)

  useEffect(() => {
    fetch(`/api/orders/${id}/`)
      .then((res) => {
        if (!res.ok) throw new Error('Not found')
        return res.json()
      })
      .then(setOrder)
      .catch(() => setError(true))
      .finally(() => setLoading(false))
  }, [id])

  if (loading) {
    return <div className="mx-auto max-w-3xl px-6 py-16 text-neutral-500">Loading...</div>
  }

  if (error || !order) {
    return (
      <div className="mx-auto max-w-3xl px-6 py-16 text-center">
        <p className="text-lg text-neutral-900">Order not found</p>
        <Link to="/account" className="mt-2 inline-block text-sm text-neutral-900 underline">
          Back to account
        </Link>
      </div>
    )
  }

  const idx = statusIndex[order.status] ?? 0

  return (
    <div className="mx-auto max-w-3xl px-6 py-16 lg:px-12">
      <Link to="/account" className="mb-8 inline-block text-sm text-neutral-500 hover:text-neutral-900">
        &larr; Back to account
      </Link>

      <div className="mb-8">
        <h1 className="text-2xl font-semibold text-neutral-900">Order #{order.id}</h1>
        <p className="text-sm text-neutral-500">
          Placed on {new Date(order.created_at).toLocaleDateString()}
        </p>
      </div>

      {/* Status timeline */}
      <div className="mb-10">
        <div className="flex items-start">
          {statusSteps.map((step, i) => {
            const done = i <= idx && order.status !== 'cancelled'
            const cancelled = order.status === 'cancelled'
            return (
              <div key={step.key} className="flex-1">
                <div className="flex items-center">
                  <div
                    className={`h-4 w-4 rounded-full ${
                      cancelled && i === 0 ? 'bg-red-500' :
                      done ? 'bg-neutral-900' : 'bg-neutral-200'
                    }`}
                  />
                  {i < statusSteps.length - 1 && (
                    <div
                      className={`h-px flex-1 ${
                        done ? 'bg-neutral-900' : 'bg-neutral-200'
                      }`}
                    />
                  )}
                </div>
                <p className="mt-1 text-xs text-neutral-500">{step.label}</p>
              </div>
            )
          })}
        </div>
        {order.status === 'cancelled' && (
          <p className="mt-2 text-sm text-red-600">This order was cancelled.</p>
        )}
      </div>

      {/* Order details */}
      <div className="rounded-lg border border-neutral-200 p-6 space-y-4">
        <div>
          <p className="text-sm text-neutral-500">Quantity</p>
          <p className="text-neutral-900">{order.quantity}</p>
        </div>
        <div>
          <p className="text-sm text-neutral-500">Shipping address</p>
          <p className="text-neutral-900 whitespace-pre-wrap">{order.address}</p>
        </div>
        {order.note && (
          <div>
            <p className="text-sm text-neutral-500">Note</p>
            <p className="text-neutral-900 whitespace-pre-wrap">{order.note}</p>
          </div>
        )}
        <div>
          <p className="text-sm text-neutral-500">Last updated</p>
          <p className="text-neutral-900">{new Date(order.updated_at).toLocaleDateString()}</p>
        </div>
      </div>
    </div>
  )
}
```

- [ ] **Step 2: Commit**

```bash
git add frontend/src/pages/OrderDetailPage.tsx
git commit -m "feat: add OrderDetailPage with status timeline"
```

---

### Task 13: Update PreOrderPage with order creation form

**Files:**
- Modify: `frontend/src/pages/PreOrderPage.tsx`

- [ ] **Step 1: Rewrite PreOrderPage with order form**

Replace `frontend/src/pages/PreOrderPage.tsx` with:

```typescript
import { useState, type FormEvent } from 'react'
import { useNavigate } from 'react-router-dom'
import { useAuth } from '../contexts/AuthContext'
import ProtectedRoute from '../components/ProtectedRoute'
import Footer from '../components/Footer'

function OrderForm() {
  const navigate = useNavigate()
  const [quantity, setQuantity] = useState(1)
  const [address, setAddress] = useState('')
  const [note, setNote] = useState('')
  const [submitting, setSubmitting] = useState(false)
  const [error, setError] = useState('')

  function handleSubmit(e: FormEvent) {
    e.preventDefault()
    setSubmitting(true)
    setError('')

    fetch('/api/orders/', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ quantity, address, note }),
    })
      .then((res) => {
        if (!res.ok) return res.json().then((d) => Promise.reject(d))
        return res.json()
      })
      .then((order) => {
        navigate(`/account/orders/${order.id}`)
      })
      .catch((err) => {
        setError(err?.address?.[0] || err?.quantity?.[0] || 'Something went wrong. Please try again.')
        setSubmitting(false)
      })
  }

  return (
    <form onSubmit={handleSubmit} className="mx-auto max-w-lg text-left">
      {error && (
        <div className="mb-6 rounded-lg border border-red-200 bg-red-50 px-4 py-3 text-sm text-red-700">
          {error}
        </div>
      )}

      <label className="mb-1 block text-sm font-medium text-neutral-700">
        Quantity
      </label>
      <select
        value={quantity}
        onChange={(e) => setQuantity(Number(e.target.value))}
        className="mb-5 block w-full rounded-lg border border-neutral-300 bg-white px-4 py-3 text-neutral-900 focus:border-neutral-900 focus:outline-none"
      >
        {[1, 2, 3, 4, 5].map((n) => (
          <option key={n} value={n}>{n}</option>
        ))}
      </select>

      <label className="mb-1 block text-sm font-medium text-neutral-700">
        Shipping address
      </label>
      <textarea
        value={address}
        onChange={(e) => setAddress(e.target.value)}
        required
        rows={3}
        placeholder="Enter your full shipping address"
        className="mb-5 block w-full rounded-lg border border-neutral-300 bg-white px-4 py-3 text-neutral-900 placeholder:text-neutral-400 focus:border-neutral-900 focus:outline-none"
      />

      <label className="mb-1 block text-sm font-medium text-neutral-700">
        Note (optional)
      </label>
      <textarea
        value={note}
        onChange={(e) => setNote(e.target.value)}
        rows={2}
        placeholder="Any special instructions or notes"
        className="mb-8 block w-full rounded-lg border border-neutral-300 bg-white px-4 py-3 text-neutral-900 placeholder:text-neutral-400 focus:border-neutral-900 focus:outline-none"
      />

      <button
        type="submit"
        disabled={submitting}
        className="w-full rounded-full bg-neutral-900 px-6 py-3 text-sm font-medium text-white transition-colors hover:bg-neutral-700 disabled:opacity-50"
      >
        {submitting ? 'Placing order...' : 'Place pre-order'}
      </button>
    </form>
  )
}

export default function PreOrderPage() {
  return (
    <ProtectedRoute>
      <section className="px-6 py-32 text-center lg:px-12">
        <h1 className="fluid-heading-xl mb-6">Pre-order DeepStoa</h1>
        <p className="mb-16 text-base text-neutral-500">
          Reserve your device now. You&rsquo;ll receive updates as your order progresses.
        </p>
        <OrderForm />
      </section>
      <Footer />
    </ProtectedRoute>
  )
}
```

- [ ] **Step 2: Commit**

```bash
git add frontend/src/pages/PreOrderPage.tsx
git commit -m "feat: convert PreOrderPage to order creation form behind auth"
```

---

### Task 14: Add Account and OrderDetail routes to App.tsx

**Files:**
- Modify: `frontend/src/App.tsx`

- [ ] **Step 1: Add the new routes**

Edit `frontend/src/App.tsx`. Add imports at top:

```typescript
import AccountPage from './pages/AccountPage'
import OrderDetailPage from './pages/OrderDetailPage'
import ProtectedRoute from './components/ProtectedRoute'
```

Add routes after the existing `/auth/callback` route:

```typescript
<Route path="/account" element={<ProtectedRoute><AccountPage /></ProtectedRoute>} />
<Route path="/account/orders/:id" element={<ProtectedRoute><OrderDetailPage /></ProtectedRoute>} />
```

Final `App.tsx`:

```typescript
import { Routes, Route } from 'react-router-dom'
import { AuthProvider } from './contexts/AuthContext'
import Nav from './components/Nav'
import ProtectedRoute from './components/ProtectedRoute'
import HomePage from './pages/HomePage'
import ProductPage from './pages/ProductPage'
import UseCasesPage from './pages/UseCasesPage'
import AboutPage from './pages/AboutPage'
import PreOrderPage from './pages/PreOrderPage'
import AccountPage from './pages/AccountPage'
import OrderDetailPage from './pages/OrderDetailPage'
import AuthCallback from './pages/AuthCallback'

export default function App() {
  return (
    <AuthProvider>
      <Nav />
      <main>
        <Routes>
          <Route path="/" element={<HomePage />} />
          <Route path="/product" element={<ProductPage />} />
          <Route path="/use-cases" element={<UseCasesPage />} />
          <Route path="/about" element={<AboutPage />} />
          <Route path="/pre-order" element={<PreOrderPage />} />
          <Route path="/auth/callback" element={<AuthCallback />} />
          <Route path="/account" element={<ProtectedRoute><AccountPage /></ProtectedRoute>} />
          <Route path="/account/orders/:id" element={<ProtectedRoute><OrderDetailPage /></ProtectedRoute>} />
        </Routes>
      </main>
    </AuthProvider>
  )
}
```

- [ ] **Step 2: Commit**

```bash
git add frontend/src/App.tsx
git commit -m "feat: add Account and OrderDetail routes"
```

---

### Task 15: Verification — backend

- [ ] **Step 1: Run Django checks**

```bash
cd backend && python manage.py check
```

Expected: "System check identified no issues (0 silenced)."

- [ ] **Step 2: Verify all URLs resolve**

```bash
cd backend && python manage.py show_urls 2>/dev/null || python -c "
from django.urls import get_resolver
resolver = get_resolver()
for p in resolver.url_patterns:
    print(p.pattern)
"
```

Expected: URLs for `/api/`, `/api/auth/`, `/_allauth/` all listed.

- [ ] **Step 3: Verify migrations**

```bash
cd backend && python manage.py migrate --plan
```

Expected: All migrations applied, no unapplied migrations.

---

### Task 16: Verification — frontend

- [ ] **Step 1: TypeScript type-check**

```bash
cd frontend && npx tsc --noEmit
```

Expected: No errors.

- [ ] **Step 2: Vite build check**

```bash
cd frontend && npx vite build
```

Expected: Build succeeds with no errors.

---

### Task 17: Google Cloud Console setup (manual, requires user action)

The user must complete these steps:

1. Go to [Google Cloud Console](https://console.cloud.google.com/apis/credentials)
2. Create an OAuth 2.0 Client ID (Web application)
3. Add authorized redirect URI: `http://localhost:5173/_allauth/browser/v1/auth/provider/callback`
4. Set environment variables before running the dev server:

```bash
export GOOGLE_CLIENT_ID="your-client-id.apps.googleusercontent.com"
export GOOGLE_CLIENT_SECRET="your-client-secret"
```
