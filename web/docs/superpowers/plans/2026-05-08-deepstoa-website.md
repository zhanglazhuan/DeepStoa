# DeepStoa Website Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the DeepStoa official website — 5 pages cloning remarkable.com's visual design, with a Django-backed waitlist form.

**Architecture:** React SPA (Vite + Tailwind CSS) frontend with 5 page components and ~12 reusable section components. Django REST API backend with a single `waitlist` app. Vite dev proxy forwards `/api/*` to Django.

**Tech Stack:** React 19, Vite 6, Tailwind CSS 4, React Router v6, Django 5, Django REST Framework

---

### Task 1: Scaffold Django backend

**Files:**
- Create: `backend/manage.py`, `backend/config/__init__.py`, `backend/config/settings.py`, `backend/config/urls.py`, `backend/config/wsgi.py`, `backend/waitlist/__init__.py`, `backend/requirements.txt`

- [ ] **Step 1: Create backend directory and install Django**

```bash
cd D:/Codes/deepstoa
mkdir -p backend/config backend/waitlist
```

- [ ] **Step 2: Write requirements.txt**

`backend/requirements.txt`:
```
Django>=5.0,<6.0
djangorestframework>=3.15,<4.0
django-cors-headers>=4.0,<5.0
```

- [ ] **Step 3: Install requirements**

```bash
cd D:/Codes/deepstoa/backend
pip install -r requirements.txt
```

- [ ] **Step 4: Write Django settings**

`backend/config/settings.py`:
```python
import os
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent

SECRET_KEY = os.environ.get('DJANGO_SECRET_KEY', 'dev-secret-key-change-in-prod')
DEBUG = True
ALLOWED_HOSTS = ['*']

INSTALLED_APPS = [
    'django.contrib.admin',
    'django.contrib.auth',
    'django.contrib.contenttypes',
    'django.contrib.sessions',
    'django.contrib.messages',
    'django.contrib.staticfiles',
    'rest_framework',
    'corsheaders',
    'waitlist',
]

MIDDLEWARE = [
    'corsheaders.middleware.CorsMiddleware',
    'django.middleware.security.SecurityMiddleware',
    'django.contrib.sessions.middleware.SessionMiddleware',
    'django.middleware.common.CommonMiddleware',
    'django.middleware.csrf.CsrfViewMiddleware',
    'django.contrib.auth.middleware.AuthenticationMiddleware',
    'django.contrib.messages.middleware.MessageMiddleware',
]

ROOT_URLCONF = 'config.urls'

TEMPLATES = [{
    'BACKEND': 'django.template.backends.django.DjangoTemplates',
    'DIRS': [],
    'APP_DIRS': True,
    'OPTIONS': {'context_processors': [
        'django.template.context_processors.debug',
        'django.template.context_processors.request',
        'django.contrib.auth.context_processors.auth',
        'django.contrib.messages.context_processors.messages',
    ]},
}]

WSGI_APPLICATION = 'config.wsgi.application'

DATABASES = {
    'default': {
        'ENGINE': 'django.db.backends.sqlite3',
        'NAME': BASE_DIR / 'db.sqlite3',
    }
}

LANGUAGE_CODE = 'en-us'
TIME_ZONE = 'UTC'
USE_I18N = False
DEFAULT_AUTO_FIELD = 'django.db.models.BigAutoField'
STATIC_URL = 'static/'

CORS_ALLOW_ALL_ORIGINS = True
```

- [ ] **Step 5: Write config/wsgi.py**

`backend/config/wsgi.py`:
```python
import os
from django.core.wsgi import get_wsgi_application
os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'config.settings')
application = get_wsgi_application()
```

- [ ] **Step 6: Write config/urls.py**

`backend/config/urls.py`:
```python
from django.contrib import admin
from django.urls import path, include

urlpatterns = [
    path('admin/', admin.site.urls),
    path('api/', include('waitlist.urls')),
]
```

- [ ] **Step 7: Write manage.py**

`backend/manage.py`:
```python
#!/usr/bin/env python
import os
import sys

def main():
    os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'config.settings')
    from django.core.management import execute_from_command_line
    execute_from_command_line(sys.argv)

if __name__ == '__main__':
    main()
```

- [ ] **Step 8: Create empty __init__.py files**

```bash
touch D:/Codes/deepstoa/backend/config/__init__.py D:/Codes/deepstoa/backend/waitlist/__init__.py
```

- [ ] **Step 9: Run migrations and verify Django starts**

```bash
cd D:/Codes/deepstoa/backend
python manage.py migrate
python manage.py runserver 8000 &
sleep 3
curl http://localhost:8000/admin/ -s -o /dev/null -w "%{http_code}"
# Expected: 200 (admin login page)
kill %1 2>/dev/null
```

- [ ] **Step 10: Commit**

```bash
cd D:/Codes/deepstoa
git add backend/
git commit -m "feat: scaffold Django backend with DRF and CORS"
```

---

### Task 2: Scaffold React frontend with Vite + Tailwind

**Files:**
- Create: `frontend/` via Vite, then configure Tailwind

- [ ] **Step 1: Create Vite React TypeScript project**

```bash
cd D:/Codes/deepstoa
npm create vite@latest frontend -- --template react-ts
cd frontend
npm install
```

- [ ] **Step 2: Install Tailwind CSS and dependencies**

```bash
cd D:/Codes/deepstoa/frontend
npm install -D tailwindcss @tailwindcss/vite
```

- [ ] **Step 3: Configure Vite with Tailwind plugin**

Read `frontend/vite.config.ts`, then write:

`frontend/vite.config.ts`:
```typescript
import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'

export default defineConfig({
  plugins: [react(), tailwindcss()],
  server: {
    proxy: {
      '/api': 'http://localhost:8000',
    },
  },
})
```

- [ ] **Step 4: Write global CSS with Tailwind and fluid typography**

Read `frontend/src/index.css`, then write:

`frontend/src/index.css`:
```css
@import "tailwindcss";

@theme {
  --color-paper: #fafaf9;
  --color-paper-dark: #f0efed;
  --color-paper-border: #e6e4e0;
}

@layer base {
  body {
    @apply bg-paper text-neutral-900 antialiased;
    font-family: 'Inter', system-ui, -apple-system, sans-serif;
  }
}

@layer utilities {
  .fluid-heading-xl {
    font-size: clamp(2.25rem, 5vw, 4.5rem);
    font-weight: 300;
    line-height: 1.1;
  }
  .fluid-heading-lg {
    font-size: clamp(1.75rem, 3.5vw, 3rem);
    font-weight: 300;
    line-height: 1.2;
  }
  .fluid-heading-md {
    font-size: clamp(1.25rem, 2.5vw, 2rem);
    font-weight: 400;
    line-height: 1.3;
  }
}
```

- [ ] **Step 5: Clean up Vite boilerplate**

Remove unused files and simplify `main.tsx` and `App.tsx`.

Delete:
```bash
rm -f D:/Codes/deepstoa/frontend/src/App.css
rm -f D:/Codes/deepstoa/frontend/src/assets/react.svg
```

Write `frontend/src/main.tsx`:
```typescript
import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { BrowserRouter } from 'react-router-dom'
import App from './App'
import './index.css'

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <BrowserRouter>
      <App />
    </BrowserRouter>
  </StrictMode>
)
```

Write `frontend/src/App.tsx`:
```typescript
import { Routes, Route } from 'react-router-dom'

export default function App() {
  return (
    <Routes>
      <Route path="/" element={<div className="fluid-heading-xl p-20">DeepStoa</div>} />
    </Routes>
  )
}
```

- [ ] **Step 6: Verify frontend starts**

```bash
cd D:/Codes/deepstoa/frontend
npm run dev &
sleep 3
curl http://localhost:5173 -s -o /dev/null -w "%{http_code}"
# Expected: 200
kill %1 2>/dev/null
```

- [ ] **Step 7: Commit**

```bash
cd D:/Codes/deepstoa
git add frontend/
git commit -m "feat: scaffold React frontend with Vite and Tailwind CSS"
```

---

### Task 3: Build shared Nav and Footer components

**Files:**
- Create: `frontend/src/components/Nav.tsx`, `frontend/src/components/Footer.tsx`

- [ ] **Step 1: Write Nav component**

`frontend/src/components/Nav.tsx`:
```typescript
import { Link, useLocation } from 'react-router-dom'

const links = [
  { to: '/product', label: 'Product' },
  { to: '/use-cases', label: 'Use Cases' },
  { to: '/about', label: 'About' },
]

export default function Nav() {
  const { pathname } = useLocation()

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

- [ ] **Step 2: Write Footer component**

`frontend/src/components/Footer.tsx`:
```typescript
import { Link } from 'react-router-dom'

const columns = [
  {
    title: 'Product',
    links: [
      { to: '/product', label: 'Overview' },
      { to: '/product', label: 'Features' },
      { to: '/product', label: 'Specs' },
    ],
  },
  {
    title: 'Use Cases',
    links: [
      { to: '/use-cases', label: 'Reading' },
      { to: '/use-cases', label: 'Flashcards' },
      { to: '/use-cases', label: 'Time Management' },
    ],
  },
  {
    title: 'Company',
    links: [
      { to: '/about', label: 'About' },
      { to: '/about', label: 'Contact' },
    ],
  },
  {
    title: 'Legal',
    links: [
      { to: '#', label: 'Privacy Policy' },
      { to: '#', label: 'Terms of Service' },
    ],
  },
]

export default function Footer() {
  return (
    <footer className="bg-neutral-900 px-6 py-20 text-neutral-400 lg:px-12">
      <div className="mx-auto max-w-7xl">
        <div className="grid grid-cols-2 gap-8 md:grid-cols-4">
          {columns.map((col) => (
            <div key={col.title}>
              <h4 className="mb-4 text-sm font-medium text-neutral-300">{col.title}</h4>
              <ul className="space-y-2">
                {col.links.map((link) => (
                  <li key={link.label}>
                    <Link
                      to={link.to}
                      className="text-sm transition-colors hover:text-white"
                    >
                      {link.label}
                    </Link>
                  </li>
                ))}
              </ul>
            </div>
          ))}
        </div>
        <div className="mt-16 border-t border-neutral-800 pt-8">
          <p className="text-sm">&copy; {new Date().getFullYear()} DeepStoa. All rights reserved.</p>
        </div>
      </div>
    </footer>
  )
}
```

- [ ] **Step 3: Commit**

```bash
cd D:/Codes/deepstoa
git add frontend/src/components/Nav.tsx frontend/src/components/Footer.tsx
git commit -m "feat: add Nav and Footer shared components"
```

---

### Task 4: Build Homepage section components — Hero, ValueProp, PersonaCards

**Files:**
- Create: `frontend/src/components/HeroSection.tsx`, `frontend/src/components/ValuePropSection.tsx`, `frontend/src/components/PersonaCards.tsx`

- [ ] **Step 1: Write HeroSection**

`frontend/src/components/HeroSection.tsx`:
```typescript
import { Link } from 'react-router-dom'

export default function HeroSection() {
  return (
    <section className="flex min-h-[90vh] items-center justify-center bg-gradient-to-b from-paper to-paper-dark px-6 py-24 text-center lg:px-12">
      <div className="max-w-3xl">
        <h1 className="fluid-heading-xl mb-6">
          The Focused Learning Device
        </h1>
        <p className="mx-auto mb-10 max-w-xl text-lg text-neutral-600">
          Distraction-free education. Paper-like reading, flashcard learning,
          and time management — in a device the size of a phone.
        </p>
        <Link
          to="/pre-order"
          className="inline-block rounded-full bg-neutral-900 px-8 py-4 text-base font-medium text-white transition-colors hover:bg-neutral-700"
        >
          Pre-order now
        </Link>
      </div>
    </section>
  )
}
```

- [ ] **Step 2: Write ValuePropSection**

`frontend/src/components/ValuePropSection.tsx`:
```typescript
export default function ValuePropSection() {
  return (
    <section className="px-6 py-36 text-center lg:px-12">
      <h2 className="fluid-heading-lg mb-6">Connect to what matters</h2>
      <p className="mx-auto max-w-lg text-base leading-relaxed text-neutral-600">
        DeepStoa replaces scattered books, paper flashcards, and printed planners
        with a single focused device that helps students learn better — without
        the distractions of a smartphone.
      </p>
    </section>
  )
}
```

- [ ] **Step 3: Write PersonaCards**

`frontend/src/components/PersonaCards.tsx`:
```typescript
const personas = [
  {
    title: 'Students',
    body: 'Read textbooks, review flashcards, and manage study schedules on one e-ink device. No notifications, no games, no distractions.',
  },
  {
    title: 'Teachers',
    body: 'Create and distribute learning materials. Share flashcard decks with your class and track engagement.',
  },
  {
    title: 'Parents',
    body: 'Give your child a screen that won\'t strain their eyes or distract them from what matters most: learning.',
  },
]

export default function PersonaCards() {
  return (
    <section className="px-6 py-24 lg:px-12">
      <div className="mx-auto max-w-7xl">
        <p className="mb-16 text-center text-xs font-medium uppercase tracking-[0.2em] text-neutral-400">
          Who is DeepStoa for?
        </p>
        <div className="flex gap-6 overflow-x-auto pb-4 md:grid md:grid-cols-3 md:overflow-visible">
          {personas.map((p) => (
            <div
              key={p.title}
              className="min-w-[280px] rounded-sm bg-white px-8 py-10 shadow-sm md:min-w-0"
            >
              <h3 className="mb-3 text-xl font-medium text-neutral-900">{p.title}</h3>
              <p className="text-sm leading-relaxed text-neutral-600">{p.body}</p>
            </div>
          ))}
        </div>
      </div>
    </section>
  )
}
```

- [ ] **Step 4: Commit**

```bash
cd D:/Codes/deepstoa
git add frontend/src/components/HeroSection.tsx frontend/src/components/ValuePropSection.tsx frontend/src/components/PersonaCards.tsx
git commit -m "feat: add HeroSection, ValuePropSection, PersonaCards components"
```

---

### Task 5: Build Homepage section components — Testimonials, CTA, Newsletter

**Files:**
- Create: `frontend/src/components/TestimonialSection.tsx`, `frontend/src/components/CTABanner.tsx`, `frontend/src/components/NewsletterSignup.tsx`

- [ ] **Step 1: Write TestimonialSection**

`frontend/src/components/TestimonialSection.tsx`:
```typescript
const testimonials = [
  {
    quote: "DeepStoa helped my daughter build a daily reading habit. The e-ink screen means she reads before bed without blue light exposure.",
    name: 'Sarah Chen',
    role: 'Parent of 3rd grader',
  },
  {
    quote: "I use DeepStoa to prep lesson plans and share flashcard decks with my students. It's become an essential part of my classroom.",
    name: 'James Liu',
    role: 'Middle school teacher',
  },
  {
    quote: "Finally, a device that doesn't try to distract me. My flashcard review sessions are twice as productive now.",
    name: 'Maya Patel',
    role: 'High school student',
  },
]

export default function TestimonialSection() {
  return (
    <section className="px-6 py-36 lg:px-12">
      <div className="mx-auto max-w-7xl">
        <h2 className="fluid-heading-lg mb-20 text-center">
          Stories from our community
        </h2>
        <div className="grid gap-12 md:grid-cols-3">
          {testimonials.map((t) => (
            <blockquote key={t.name} className="flex flex-col">
              <p className="mb-6 flex-1 text-base leading-relaxed text-neutral-700">
                &ldquo;{t.quote}&rdquo;
              </p>
              <footer>
                <cite className="block text-sm font-medium not-italic text-neutral-900">
                  {t.name}
                </cite>
                <span className="text-sm text-neutral-500">{t.role}</span>
              </footer>
            </blockquote>
          ))}
        </div>
      </div>
    </section>
  )
}
```

- [ ] **Step 2: Write CTABanner**

`frontend/src/components/CTABanner.tsx`:
```typescript
import { Link } from 'react-router-dom'

export default function CTABanner() {
  return (
    <section className="mx-6 mb-24 rounded-sm bg-neutral-900 px-8 py-28 text-center text-white lg:mx-12 lg:px-12">
      <h2 className="fluid-heading-lg mb-6">Try it in your classroom</h2>
      <p className="mx-auto mb-10 max-w-md text-base leading-relaxed text-neutral-400">
        Be among the first to experience DeepStoa. Join the waitlist today.
      </p>
      <Link
        to="/pre-order"
        className="inline-block rounded-full bg-white px-8 py-4 text-base font-medium text-neutral-900 transition-colors hover:bg-neutral-200"
      >
        Join waitlist
      </Link>
    </section>
  )
}
```

- [ ] **Step 3: Write NewsletterSignup**

`frontend/src/components/NewsletterSignup.tsx`:
```typescript
import { useState } from 'react'

export default function NewsletterSignup() {
  const [email, setEmail] = useState('')
  const [submitted, setSubmitted] = useState(false)

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault()
    if (email) setSubmitted(true)
  }

  return (
    <section className="px-6 py-20 lg:px-12">
      <div className="mx-auto max-w-lg text-center">
        <h2 className="mb-2 text-xl font-medium text-neutral-900">
          Be the first to know
        </h2>
        <p className="mb-8 text-sm text-neutral-500">
          Get updates on DeepStoa's launch and features.
        </p>
        {submitted ? (
          <p className="text-sm text-green-700">Thank you for subscribing.</p>
        ) : (
          <form onSubmit={handleSubmit} className="flex gap-3">
            <input
              type="email"
              value={email}
              onChange={(e) => setEmail(e.target.value)}
              placeholder="Email address"
              required
              className="flex-1 rounded-sm border border-paper-border bg-white px-4 py-3 text-sm text-neutral-900 placeholder-neutral-400 outline-none focus:border-neutral-400"
            />
            <button
              type="submit"
              className="rounded-sm bg-neutral-900 px-6 py-3 text-sm font-medium text-white transition-colors hover:bg-neutral-700"
            >
              Sign up
            </button>
          </form>
        )}
      </div>
    </section>
  )
}
```

- [ ] **Step 4: Commit**

```bash
cd D:/Codes/deepstoa
git add frontend/src/components/TestimonialSection.tsx frontend/src/components/CTABanner.tsx frontend/src/components/NewsletterSignup.tsx
git commit -m "feat: add TestimonialSection, CTABanner, NewsletterSignup components"
```

---

### Task 6: Assemble HomePage

**Files:**
- Modify: `frontend/src/pages/HomePage.tsx` (create), `frontend/src/App.tsx`

- [ ] **Step 1: Write HomePage**

`frontend/src/pages/HomePage.tsx`:
```typescript
import HeroSection from '../components/HeroSection'
import ValuePropSection from '../components/ValuePropSection'
import PersonaCards from '../components/PersonaCards'
import TestimonialSection from '../components/TestimonialSection'
import CTABanner from '../components/CTABanner'
import NewsletterSignup from '../components/NewsletterSignup'
import Footer from '../components/Footer'

export default function HomePage() {
  return (
    <>
      <HeroSection />
      <ValuePropSection />
      <PersonaCards />
      <TestimonialSection />
      <CTABanner />
      <NewsletterSignup />
      <Footer />
    </>
  )
}
```

- [ ] **Step 2: Update App.tsx with routes**

Read `frontend/src/App.tsx`, then write:

`frontend/src/App.tsx`:
```typescript
import { Routes, Route } from 'react-router-dom'
import Nav from './components/Nav'
import HomePage from './pages/HomePage'

export default function App() {
  return (
    <>
      <Nav />
      <main>
        <Routes>
          <Route path="/" element={<HomePage />} />
        </Routes>
      </main>
    </>
  )
}
```

- [ ] **Step 3: Start frontend and verify homepage renders**

```bash
cd D:/Codes/deepstoa/frontend
npm run dev &
sleep 3
curl http://localhost:5173 -s | grep -o "The Focused Learning Device"
# Expected: "The Focused Learning Device"
kill %1 2>/dev/null
```

- [ ] **Step 4: Commit**

```bash
cd D:/Codes/deepstoa
git add frontend/src/pages/HomePage.tsx frontend/src/App.tsx
git commit -m "feat: assemble HomePage with all sections"
```

---

### Task 7: Build Product page

**Files:**
- Create: `frontend/src/components/FeatureRow.tsx`, `frontend/src/components/SpecsTable.tsx`, `frontend/src/pages/ProductPage.tsx`
- Modify: `frontend/src/App.tsx`

- [ ] **Step 1: Write FeatureRow component**

`frontend/src/components/FeatureRow.tsx`:
```typescript
interface FeatureRowProps {
  title: string
  body: string
  imageLeft?: boolean
}

export default function FeatureRow({ title, body, imageLeft }: FeatureRowProps) {
  const image = (
    <div className="flex min-h-[280px] items-center justify-center rounded-sm bg-paper-dark">
      <span className="text-sm text-neutral-400">Device image</span>
    </div>
  )

  const text = (
    <div>
      <h3 className="fluid-heading-md mb-4">{title}</h3>
      <p className="text-base leading-relaxed text-neutral-600">{body}</p>
    </div>
  )

  return (
    <div className="grid items-center gap-12 md:grid-cols-2">
      {imageLeft ? image : text}
      {imageLeft ? text : image}
    </div>
  )
}
```

- [ ] **Step 2: Write SpecsTable component**

`frontend/src/components/SpecsTable.tsx`:
```typescript
const specs = [
  ['Screen', '3.97" E-ink, 800×480, dual-point touch'],
  ['Processor', 'ESP32S3'],
  ['Operating System', 'Zephyr RTOS'],
  ['Connectivity', 'Wi-Fi 802.11 b/g/n, Bluetooth LE 5.0'],
  ['Battery', 'Weeks on a single charge'],
  ['Storage', '16MB flash'],
  ['Charging', 'USB-C'],
]

export default function SpecsTable() {
  return (
    <div>
      <h3 className="fluid-heading-md mb-10 text-center">Technical specifications</h3>
      <div className="mx-auto max-w-2xl">
        {specs.map(([label, value]) => (
          <div key={label} className="flex border-b border-paper-border py-4 text-sm">
            <dt className="w-40 shrink-0 text-neutral-400">{label}</dt>
            <dd className="text-neutral-900">{value}</dd>
          </div>
        ))}
      </div>
    </div>
  )
}
```

- [ ] **Step 3: Write ProductPage**

`frontend/src/pages/ProductPage.tsx`:
```typescript
import FeatureRow from '../components/FeatureRow'
import SpecsTable from '../components/SpecsTable'
import Footer from '../components/Footer'

export default function ProductPage() {
  return (
    <>
      <section className="px-6 py-32 text-center lg:px-12">
        <h1 className="fluid-heading-xl mb-6">The device built for learning</h1>
        <p className="text-base text-neutral-500">
          3.97" E-ink &middot; 800&times;480 &middot; Dual-touch &middot; ESP32S3 &middot; Zephyr OS
        </p>
      </section>

      <section className="mx-auto max-w-7xl space-y-32 px-6 pb-32 lg:px-12">
        <FeatureRow
          title="E-ink display. Zero eye strain."
          body="No backlight, no blue light, no glare. Students read comfortably for hours — just like paper, but smarter. The 3.97\" display is the perfect size for small hands."
          imageLeft
        />
        <FeatureRow
          title="Built for focus. Nothing else."
          body="No games, no social media, no notifications. DeepStoa does three things — reading, flashcard learning, and time management — and does them well."
        />
        <FeatureRow
          title="Weeks of battery. Zero friction."
          body="E-ink sips power. Students go weeks between charges. USB-C charging means one cable for everything. No charger anxiety, no daily plug-in ritual."
          imageLeft
        />
      </section>

      <section className="px-6 pb-32 lg:px-12">
        <SpecsTable />
      </section>

      <Footer />
    </>
  )
}
```

- [ ] **Step 4: Add route to App.tsx**

Read `frontend/src/App.tsx`, then write:

`frontend/src/App.tsx`:
```typescript
import { Routes, Route } from 'react-router-dom'
import Nav from './components/Nav'
import HomePage from './pages/HomePage'
import ProductPage from './pages/ProductPage'

export default function App() {
  return (
    <>
      <Nav />
      <main>
        <Routes>
          <Route path="/" element={<HomePage />} />
          <Route path="/product" element={<ProductPage />} />
        </Routes>
      </main>
    </>
  )
}
```

- [ ] **Step 5: Commit**

```bash
cd D:/Codes/deepstoa
git add frontend/src/components/FeatureRow.tsx frontend/src/components/SpecsTable.tsx frontend/src/pages/ProductPage.tsx frontend/src/App.tsx
git commit -m "feat: add Product page with feature rows and specs table"
```

---

### Task 8: Build Use Cases page

**Files:**
- Create: `frontend/src/components/UseCaseSection.tsx`, `frontend/src/pages/UseCasesPage.tsx`
- Modify: `frontend/src/App.tsx`

- [ ] **Step 1: Write UseCaseSection component**

`frontend/src/components/UseCaseSection.tsx`:
```typescript
interface UseCaseSectionProps {
  title: string
  body: string
}

export default function UseCaseSection({ title, body }: UseCaseSectionProps) {
  return (
    <div className="border-b border-paper-border py-20 text-center last:border-none">
      <h2 className="fluid-heading-md mb-4">{title}</h2>
      <p className="mx-auto max-w-lg text-base leading-relaxed text-neutral-600">{body}</p>
    </div>
  )
}
```

- [ ] **Step 2: Write UseCasesPage**

`frontend/src/pages/UseCasesPage.tsx`:
```typescript
import UseCaseSection from '../components/UseCaseSection'
import Footer from '../components/Footer'

const useCases = [
  {
    title: 'Reading',
    body: 'Load textbooks, novels, and articles. E-ink reads like paper — no eye fatigue even after hours. Adjustable fonts and sizes for every reader. Highlight, annotate, and bookmark without distraction.',
  },
  {
    title: 'Flashcard Learning',
    body: 'Create decks or import shared ones. Built-in spaced repetition helps students retain knowledge longer. The tactile touch screen makes flipping cards feel natural and satisfying.',
  },
  {
    title: 'Time Management',
    body: 'Pomodoro timer, study planner, and schedule view. Helps students build focus habits without a smartphone nearby. Set goals, track progress, and celebrate streaks.',
  },
]

export default function UseCasesPage() {
  return (
    <>
      <section className="px-6 py-32 text-center lg:px-12">
        <p className="mb-6 text-xs font-medium uppercase tracking-[0.2em] text-neutral-400">
          What DeepStoa can do
        </p>
        <h1 className="fluid-heading-xl">Three tools. One device.</h1>
      </section>

      <section className="mx-auto max-w-3xl px-6 pb-32 lg:px-12">
        {useCases.map((uc) => (
          <UseCaseSection key={uc.title} title={uc.title} body={uc.body} />
        ))}
      </section>

      <Footer />
    </>
  )
}
```

- [ ] **Step 3: Add route**

Read `frontend/src/App.tsx`, then write:

`frontend/src/App.tsx`:
```typescript
import { Routes, Route } from 'react-router-dom'
import Nav from './components/Nav'
import HomePage from './pages/HomePage'
import ProductPage from './pages/ProductPage'
import UseCasesPage from './pages/UseCasesPage'

export default function App() {
  return (
    <>
      <Nav />
      <main>
        <Routes>
          <Route path="/" element={<HomePage />} />
          <Route path="/product" element={<ProductPage />} />
          <Route path="/use-cases" element={<UseCasesPage />} />
        </Routes>
      </main>
    </>
  )
}
```

- [ ] **Step 4: Commit**

```bash
cd D:/Codes/deepstoa
git add frontend/src/components/UseCaseSection.tsx frontend/src/pages/UseCasesPage.tsx frontend/src/App.tsx
git commit -m "feat: add Use Cases page with three sections"
```

---

### Task 9: Build About page

**Files:**
- Create: `frontend/src/components/StatsRow.tsx`, `frontend/src/pages/AboutPage.tsx`
- Modify: `frontend/src/App.tsx`

- [ ] **Step 1: Write StatsRow component**

`frontend/src/components/StatsRow.tsx`:
```typescript
const stats = [
  ['2024', 'Founded'],
  ['50+', 'Schools piloting'],
  ['3.97"', 'Perfect size'],
  ['0', 'Distractions'],
]

export default function StatsRow() {
  return (
    <div className="grid grid-cols-2 gap-8 md:grid-cols-4">
      {stats.map(([value, label]) => (
        <div key={label} className="text-center">
          <p className="fluid-heading-md mb-1">{value}</p>
          <p className="text-sm text-neutral-500">{label}</p>
        </div>
      ))}
    </div>
  )
}
```

- [ ] **Step 2: Write AboutPage**

`frontend/src/pages/AboutPage.tsx`:
```typescript
import StatsRow from '../components/StatsRow'
import Footer from '../components/Footer'

export default function AboutPage() {
  return (
    <>
      <section className="px-6 py-32 text-center lg:px-12">
        <h1 className="fluid-heading-xl mb-8">Learning deserves better tools</h1>
        <p className="mx-auto max-w-xl text-base leading-relaxed text-neutral-600">
          We believe students learn best when technology fades into the background.
          DeepStoa was built to give K-12 students the power of digital learning —
          without the distraction and eye strain of traditional screens.
        </p>
      </section>

      <section className="mx-auto max-w-4xl px-6 pb-40 lg:px-12">
        <StatsRow />
      </section>

      <Footer />
    </>
  )
}
```

- [ ] **Step 3: Add route**

Read `frontend/src/App.tsx`, then write:

`frontend/src/App.tsx`:
```typescript
import { Routes, Route } from 'react-router-dom'
import Nav from './components/Nav'
import HomePage from './pages/HomePage'
import ProductPage from './pages/ProductPage'
import UseCasesPage from './pages/UseCasesPage'
import AboutPage from './pages/AboutPage'

export default function App() {
  return (
    <>
      <Nav />
      <main>
        <Routes>
          <Route path="/" element={<HomePage />} />
          <Route path="/product" element={<ProductPage />} />
          <Route path="/use-cases" element={<UseCasesPage />} />
          <Route path="/about" element={<AboutPage />} />
        </Routes>
      </main>
    </>
  )
}
```

- [ ] **Step 4: Commit**

```bash
cd D:/Codes/deepstoa
git add frontend/src/components/StatsRow.tsx frontend/src/pages/AboutPage.tsx frontend/src/App.tsx
git commit -m "feat: add About page with mission and stats"
```

---

### Task 10: Build Pre-order page with waitlist form

**Files:**
- Create: `frontend/src/components/WaitlistForm.tsx`, `frontend/src/pages/PreOrderPage.tsx`
- Modify: `frontend/src/App.tsx`

- [ ] **Step 1: Write WaitlistForm component**

`frontend/src/components/WaitlistForm.tsx`:
```typescript
import { useState } from 'react'

type Role = 'student' | 'teacher' | 'parent'

interface FormData {
  name: string
  email: string
  role: Role
}

const INITIAL: FormData = { name: '', email: '', role: 'student' }

export default function WaitlistForm() {
  const [form, setForm] = useState<FormData>(INITIAL)
  const [submitted, setSubmitted] = useState(false)
  const [error, setError] = useState('')
  const [loading, setLoading] = useState(false)

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault()
    setError('')
    setLoading(true)

    try {
      const res = await fetch('/api/waitlist/', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(form),
      })

      if (res.ok) {
        setSubmitted(true)
      } else {
        const data = await res.json()
        setError(data.error || 'Something went wrong.')
      }
    } catch {
      setError('Network error. Please try again.')
    } finally {
      setLoading(false)
    }
  }

  if (submitted) {
    return (
      <div className="text-center">
        <p className="fluid-heading-md mb-4">You're on the list</p>
        <p className="text-base text-neutral-500">
          We'll notify you when DeepStoa launches.
        </p>
      </div>
    )
  }

  return (
    <form onSubmit={handleSubmit} className="mx-auto max-w-sm">
      <div className="space-y-4">
        <input
          type="text"
          placeholder="Full name"
          required
          value={form.name}
          onChange={(e) => setForm({ ...form, name: e.target.value })}
          className="w-full rounded-sm border border-paper-border bg-white px-4 py-3 text-sm text-neutral-900 placeholder-neutral-400 outline-none focus:border-neutral-400"
        />
        <input
          type="email"
          placeholder="Email address"
          required
          value={form.email}
          onChange={(e) => setForm({ ...form, email: e.target.value })}
          className="w-full rounded-sm border border-paper-border bg-white px-4 py-3 text-sm text-neutral-900 placeholder-neutral-400 outline-none focus:border-neutral-400"
        />
        <fieldset className="text-left">
          <legend className="mb-3 text-sm text-neutral-500">I am a...</legend>
          <div className="flex gap-4">
            {(['student', 'teacher', 'parent'] as Role[]).map((role) => (
              <label key={role} className="flex items-center gap-2 text-sm text-neutral-700">
                <input
                  type="radio"
                  name="role"
                  value={role}
                  checked={form.role === role}
                  onChange={(e) => setForm({ ...form, role: e.target.value as Role })}
                />
                {role.charAt(0).toUpperCase() + role.slice(1)}
              </label>
            ))}
          </div>
        </fieldset>
      </div>
      {error && <p className="mt-4 text-sm text-red-600">{error}</p>}
      <button
        type="submit"
        disabled={loading}
        className="mt-8 w-full rounded-sm bg-neutral-900 py-3.5 text-sm font-medium text-white transition-colors hover:bg-neutral-700 disabled:opacity-50"
      >
        {loading ? 'Submitting...' : 'Submit'}
      </button>
      <p className="mt-4 text-xs text-neutral-400">No payment required. We'll notify you when DeepStoa launches.</p>
    </form>
  )
}
```

- [ ] **Step 2: Write PreOrderPage**

`frontend/src/pages/PreOrderPage.tsx`:
```typescript
import WaitlistForm from '../components/WaitlistForm'
import Footer from '../components/Footer'

export default function PreOrderPage() {
  return (
    <>
      <section className="px-6 py-32 text-center lg:px-12">
        <h1 className="fluid-heading-xl mb-6">Join the waitlist</h1>
        <p className="mb-16 text-base text-neutral-500">
          Be the first to bring DeepStoa to your school or home.
        </p>
        <WaitlistForm />
      </section>
      <Footer />
    </>
  )
}
```

- [ ] **Step 3: Add route**

Read `frontend/src/App.tsx`, then write:

`frontend/src/App.tsx`:
```typescript
import { Routes, Route } from 'react-router-dom'
import Nav from './components/Nav'
import HomePage from './pages/HomePage'
import ProductPage from './pages/ProductPage'
import UseCasesPage from './pages/UseCasesPage'
import AboutPage from './pages/AboutPage'
import PreOrderPage from './pages/PreOrderPage'

export default function App() {
  return (
    <>
      <Nav />
      <main>
        <Routes>
          <Route path="/" element={<HomePage />} />
          <Route path="/product" element={<ProductPage />} />
          <Route path="/use-cases" element={<UseCasesPage />} />
          <Route path="/about" element={<AboutPage />} />
          <Route path="/pre-order" element={<PreOrderPage />} />
        </Routes>
      </main>
    </>
  )
}
```

- [ ] **Step 4: Commit**

```bash
cd D:/Codes/deepstoa
git add frontend/src/components/WaitlistForm.tsx frontend/src/pages/PreOrderPage.tsx frontend/src/App.tsx
git commit -m "feat: add Pre-order page with waitlist form"
```

---

### Task 11: Build Django waitlist API

**Files:**
- Create: `backend/waitlist/models.py`, `backend/waitlist/serializers.py`, `backend/waitlist/views.py`, `backend/waitlist/urls.py`

- [ ] **Step 1: Write WaitlistEntry model**

`backend/waitlist/models.py`:
```python
from django.db import models

class WaitlistEntry(models.Model):
    name = models.CharField(max_length=200)
    email = models.EmailField(unique=True)
    role = models.CharField(max_length=20, choices=[
        ('student', 'Student'),
        ('teacher', 'Teacher'),
        ('parent', 'Parent'),
    ])
    created_at = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        return f"{self.name} <{self.email}>"
```

- [ ] **Step 2: Write serializer**

`backend/waitlist/serializers.py`:
```python
from rest_framework import serializers
from .models import WaitlistEntry

class WaitlistEntrySerializer(serializers.ModelSerializer):
    class Meta:
        model = WaitlistEntry
        fields = ['id', 'name', 'email', 'role', 'created_at']
        read_only_fields = ['id', 'created_at']
```

- [ ] **Step 3: Write view**

`backend/waitlist/views.py`:
```python
from rest_framework import generics, status
from rest_framework.response import Response
from .models import WaitlistEntry
from .serializers import WaitlistEntrySerializer

class WaitlistCreateView(generics.CreateAPIView):
    queryset = WaitlistEntry.objects.all()
    serializer_class = WaitlistEntrySerializer

    def create(self, request, *args, **kwargs):
        serializer = self.get_serializer(data=request.data)
        if serializer.is_valid():
            serializer.save()
            return Response(serializer.data, status=status.HTTP_201_CREATED)
        if 'email' in serializer.errors:
            return Response(
                {'error': 'This email is already registered on the waitlist.'},
                status=status.HTTP_400_BAD_REQUEST,
            )
        return Response(serializer.errors, status=status.HTTP_400_BAD_REQUEST)
```

- [ ] **Step 4: Write URL config**

`backend/waitlist/urls.py`:
```python
from django.urls import path
from . import views

urlpatterns = [
    path('waitlist/', views.WaitlistCreateView.as_view(), name='waitlist-create'),
]
```

- [ ] **Step 5: Run makemigrations and migrate**

```bash
cd D:/Codes/deepstoa/backend
python manage.py makemigrations waitlist
python manage.py migrate
```

- [ ] **Step 6: Test API endpoint**

Start Django:
```bash
cd D:/Codes/deepstoa/backend
python manage.py runserver 8000 &
sleep 3
```

Test create:
```bash
curl -X POST http://localhost:8000/api/waitlist/ \
  -H "Content-Type: application/json" \
  -d '{"name":"Test User","email":"test@example.com","role":"student"}' \
  -s | python -m json.tool
# Expected: {"id":1,"name":"Test User","email":"test@example.com","role":"student","created_at":"..."}
```

Test duplicate:
```bash
curl -X POST http://localhost:8000/api/waitlist/ \
  -H "Content-Type: application/json" \
  -d '{"name":"Test User","email":"test@example.com","role":"student"}' \
  -s | python -m json.tool
# Expected: {"error":"This email is already registered on the waitlist."}
```

Kill server:
```bash
kill %1 2>/dev/null
```

- [ ] **Step 7: Commit**

```bash
cd D:/Codes/deepstoa
git add backend/waitlist/ backend/
git commit -m "feat: add waitlist API endpoint with duplicate email handling"
```

---

### Task 12: End-to-end verification and polish

**Files:**
- Modify: `frontend/src/index.css` (smooth scroll), `frontend/index.html` (Inter font)

- [ ] **Step 1: Add Inter font to index.html**

Read `frontend/index.html`, then write:

`frontend/index.html`:
```html
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <link rel="preconnect" href="https://fonts.googleapis.com" />
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin />
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600&display=swap" rel="stylesheet" />
    <title>DeepStoa — The Focused Learning Device</title>
  </head>
  <body>
    <div id="root"></div>
    <script type="module" src="/src/main.tsx"></script>
  </body>
</html>
```

- [ ] **Step 2: Add smooth scrolling and shared page wrapper**

Read `frontend/src/index.css`, then append:

```css
html {
  scroll-behavior: smooth;
}
```

- [ ] **Step 3: Start both servers and verify full flow**

```bash
cd D:/Codes/deepstoa/backend
python manage.py runserver 8000 &
```

```bash
cd D:/Codes/deepstoa/frontend
npm run dev &
sleep 4
```

Verify pages load:
```bash
for path in "/" "/product" "/use-cases" "/about" "/pre-order"; do
  code=$(curl -s -o /dev/null -w "%{http_code}" "http://localhost:5173$path")
  echo "$path: $code"
done
# Expected: all 200
```

Verify proxy + API:
```bash
curl -X POST http://localhost:5173/api/waitlist/ \
  -H "Content-Type: application/json" \
  -d '{"name":"E2E Test","email":"e2e@test.com","role":"teacher"}' \
  -s | python -m json.tool
# Expected: 201 with JSON response
```

Kill servers:
```bash
kill %1 %2 2>/dev/null
```

- [ ] **Step 4: Commit**

```bash
cd D:/Codes/deepstoa
git add frontend/index.html frontend/src/index.css
git commit -m "feat: add Inter font, smooth scrolling, verify e2e flow"
```
