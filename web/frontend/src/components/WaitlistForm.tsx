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
