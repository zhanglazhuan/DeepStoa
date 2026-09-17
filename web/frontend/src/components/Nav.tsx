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
