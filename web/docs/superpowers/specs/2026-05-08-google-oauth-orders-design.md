# Google OAuth Login & Order System — Design Spec

**Date**: 2026-05-08
**Status**: Approved
**Tech**: Django 5 + DRF + React 19 + Tailwind CSS 4

## Overview

Add Google OAuth authentication so users can sign in, place pre-orders, and view their order status. Multi-provider architecture (social-auth-app-django) but only Google is enabled initially.

## 1. Backend — Dependencies

Add to `requirements.txt`:
- `django-allauth[headless]` — social auth with REST endpoints, no Django templates needed
- `dj-rest-auth` — session/auth REST API bridges

## 2. Backend — Models

### 2.1 User (Django built-in, extended via allauth)
- Allauth handles `SocialAccount` model linking Google UID to Django `User`
- Fields mapped: `email` ← Google email, `first_name` ← given_name, `last_name` ← family_name

### 2.2 Order (`orders` app)

```
Order
├── user: ForeignKey → User, related_name='orders'
├── quantity: PositiveSmallIntegerField, default=1
├── address: TextField
├── note: TextField, blank=True
├── status: CharField choices=[
│     ('pending', 'Pending'),
│     ('confirmed', 'Confirmed'),
│     ('producing', 'In Production'),
│     ('shipped', 'Shipped'),
│     ('cancelled', 'Cancelled'),
│   ], default='pending'
├── created_at: DateTimeField(auto_now_add)
└── updated_at: DateTimeField(auto_now)
```

## 3. Backend — API Endpoints

### Auth (allauth headless + dj-rest-auth)

| Endpoint | Method | Auth | Description |
|---|---|---|---|
| `/api/auth/google/` | GET | None | Initiate Google OAuth flow, returns redirect URL |
| `/api/auth/google/callback/` | GET | None | Google callback — allauth processes code, creates/links User, sets session |
| `/api/auth/user/` | GET | Session | Returns `{id, email, first_name, last_name, is_authenticated}` |
| `/api/auth/logout/` | POST | Session | Destroys session |

### Orders

| Endpoint | Method | Auth | Description |
|---|---|---|---|
| `/api/orders/` | GET | Session | Current user's orders: `[{id, status, quantity, created_at}]` |
| `/api/orders/` | POST | Session | Create order: body `{quantity, address, note?}` → 201 |
| `/api/orders/<id>/` | GET | Session (owner) | Order detail, all fields. 403 if not owner |

## 4. Backend — Settings Changes

- `INSTALLED_APPS`: add `allauth`, `allauth.headless`, `allauth.socialaccount`, `allauth.socialaccount.providers.google`, `dj_rest_auth`, `orders`
- `AUTHENTICATION_BACKENDS`: add allauth backend
- `SOCIALACCOUNT_PROVIDERS`: Google provider config with `client_id` + `secret` from env vars
- `SOCIALACCOUNT_EMAIL_AUTHENTICATION`: True (use email as identifier)
- Session auth via Django's built-in session middleware (already configured)
- CSRF trusted origins for frontend dev server

## 5. Frontend — New Files

| File | Purpose |
|---|---|
| `src/contexts/AuthContext.tsx` | Auth state: `{user, loading}`, `login()`, `logout()`. On mount calls `GET /api/auth/user/` |
| `src/components/ProtectedRoute.tsx` | Wraps routes requiring auth; redirects to `/` if unauthenticated |
| `src/pages/AccountPage.tsx` | User profile card + order list |
| `src/pages/OrderDetailPage.tsx` | Single order detail with status timeline |
| `src/pages/AuthCallback.tsx` | No-UI page: on mount, polls/calls `/api/auth/user/` then navigates |

## 6. Frontend — Changed Files

| File | Change |
|---|---|
| `App.tsx` | Add new routes, wrap with `AuthProvider` |
| `Nav.tsx` | Right side: if logged in show avatar+name dropdown (Account / Sign out), else show "Sign in" button |
| `PreOrderPage.tsx` | Replace `WaitlistForm` with order creation form (quantity, address, note); POST to `/api/orders/` |

## 7. Frontend — Data Flow

```
App
└── AuthProvider (Context)
    ├── Nav
    │   ├── Links
    │   ├── GitHub icon
    │   ├── [if user] Avatar dropdown → Account / Sign out
    │   └── [if !user] "Sign in" button → /api/auth/google/
    └── Routes
        ├── /pre-order → ProtectedRoute → PreOrderPage (create order)
        ├── /account → ProtectedRoute → AccountPage (profile + order list)
        ├── /account/orders/:id → ProtectedRoute → OrderDetailPage
        └── /auth/callback → AuthCallback (process OAuth return)
```

### Auth Flow

1. User clicks "Sign in" → `window.location.href = "http://localhost:8000/api/auth/google/"`
2. Backend redirects to Google consent screen
3. Google redirects to `/api/auth/google/callback/` with auth code
4. Allauth exchanges code, creates/links user, sets session, redirects to `http://localhost:5173/auth/callback`
5. AuthCallback calls `GET /api/auth/user/` — if authenticated, navigate to previous page or home; if not, show error
6. On subsequent page loads, AuthProvider calls `/api/auth/user/` to restore session

### Logout Flow

1. User clicks "Sign out" → `POST /api/auth/logout/`
2. AuthProvider clears user state, Nav re-renders with Sign in button

### Order Creation Flow

1. User on `/pre-order` (authenticated) fills form: quantity, address, note
2. POST `/api/orders/` with form data → 201
3. Redirect to `/account/orders/:id` showing order detail

## 8. Error Handling

- **Google OAuth denied**: Redirect to `/` with toast "Sign in was cancelled"
- **Session expired**: API returns 401 → AuthProvider clears user, ProtectedRoute redirects
- **Order creation fails**: Form shows inline validation errors
- **Order not found / not owner**: 404 page or redirect to `/account`

## 9. Testing Considerations

- Backend: test OAuth callback creates user, session persists, order CRUD with owner check
- Frontend: test AuthContext state transitions, ProtectedRoute redirect, Nav conditional rendering
