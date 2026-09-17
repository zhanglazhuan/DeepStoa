# DeepStoa Website Design Spec

**Date**: 2026-05-08
**Reference**: remarkable.com — visual clone, DeepStoa content

## Overview

DeepStoa official website. 5 pages marketing an e-ink phone-form-factor device for K-12 education. Visual design cloned from remarkable.com. React SPA frontend, Django REST API backend.

## Architecture

```
deepstoa/
├── frontend/           # React + Vite + Tailwind
│   └── src/
│       ├── pages/      # 5 page components
│       ├── components/ # Reusable sections (Nav, Footer, Hero, etc.)
│       └── App.tsx     # React Router routes
├── backend/            # Django + DRF
│   ├── config/         # Settings, URLs
│   └── waitlist/       # Single app: model + view + serializer
```

**Stack**: React 19, Vite, React Router v6, Tailwind CSS 4, Django 5, Django REST Framework
**Content**: Hardcoded in React components (except waitlist entries → DB)
**API surface**: `POST /api/waitlist/` — name, email, role (student/teacher/parent)
**Dev proxy**: Vite proxies `/api/*` → Django `localhost:8000`

## Design System

### Colors (Paper Palette)
- Background: `#fafaf9` (warm white)
- Surface: `#f0efed` (light gray)
- Border: `#e6e4e0` (medium gray)
- Body text: `#1a1a1a` (near black)
- Secondary text: `#666666` (gray)
- Footer bg: `#1a1a1a` (dark)

### Typography
- Headings: Fluid scale using `clamp()` (e.g., `clamp(32px, 5vw, 72px)`)
- Body: 15-16px, sans-serif
- Subtitle: 14px, gray
- All headings lightweight (font-weight: 300-400)

### Spacing
- Section padding: 80-160px vertical
- Container max-width: 1200px
- Generous whitespace throughout

## Pages

### 1. Homepage `/`
Sections in scroll order:
1. **Nav** — Logo (DeepStoa) + links (Product, Use Cases, About, Pre-order)
2. **Hero** — Full-bleed bg, product shot, headline "The Focused Learning Device", subtext, CTA button
3. **Value Prop** — "Connect to what matters" + 2-3 sentence description
4. **Persona Cards** — Horizontal scroll cards for Students, Teachers, Parents
5. **Testimonials** — 2-3 quotes with name and role
6. **CTA Banner** — Dark bg, "Try it in your classroom", waitlist CTA
7. **Newsletter** — Email input + signup
8. **Footer** — 4-column links: Product, Use Cases, Company, Legal

### 2. Product `/product`
1. Hero — "The device built for learning" + key specs
2. Feature rows — alternating image/text, 2-3 rows (display, focused OS, content sync)
3. Specs table — full hardware/spec list

### 3. Use Cases `/use-cases`
1. Hero — "What DeepStoa can do"
2. Three stacked sections with icons/illustrations: Reading, Flashcard Learning, Time Management

### 4. About `/about`
1. Mission — "Learning deserves better tools" + company story
2. Stats row — founding year, school count, device specs

### 5. Pre-order `/pre-order`
1. Waitlist form — name, email, role radio (student/teacher/parent), submit
2. Confirmation state — "You're on the list" message

## Backend

### Waitlist Model
```python
class WaitlistEntry(models.Model):
    name = models.CharField(max_length=200)
    email = models.EmailField()
    role = models.CharField(max_length=20)  # student, teacher, parent
    created_at = models.DateTimeField(auto_now_add=True)
```

### API
| Method | Path | Body | Response |
|--------|------|------|----------|
| POST | /api/waitlist/ | {name, email, role} | 201 {id, name, email, role} |
| POST | /api/waitlist/ | (duplicate email) | 400 {error: "Email already registered"} |

## Component Tree

```
App
├── Nav (sticky, links + CTA button)
├── Routes
│   ├── HomePage
│   │   ├── HeroSection
│   │   ├── ValuePropSection
│   │   ├── PersonaCards (horizontal scroll)
│   │   ├── TestimonialSection
│   │   ├── CTABanner
│   │   ├── NewsletterSignup
│   │   └── Footer
│   ├── ProductPage
│   │   ├── FeatureRow (× 2-3)
│   │   └── SpecsTable
│   ├── UseCasesPage
│   │   └── UseCaseSection (× 3)
│   ├── AboutPage
│   │   └── StatsRow
│   └── PreOrderPage
│       └── WaitlistForm → POST /api/waitlist/
└── Footer
```

## Implementation Order

1. Scaffold: Vite React project + Django project
2. Design system: Tailwind config, global CSS, fluid typography
3. Shared components: Nav, Footer
4. Homepage (all sections)
5. Product page
6. Use Cases page
7. About page
8. Pre-order page + Django waitlist API
9. Polish: scroll animations, responsive, favicon

## Out of Scope
- CMS / admin content editing (hardcoded for now)
- Real payment processing (waitlist only)
- i18n / localization
- Blog
- User accounts / auth
