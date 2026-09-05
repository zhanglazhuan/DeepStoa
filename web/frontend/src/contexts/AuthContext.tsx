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
