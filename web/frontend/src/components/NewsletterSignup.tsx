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
