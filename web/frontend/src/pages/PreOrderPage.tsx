import { useState, type FormEvent } from 'react'
import { useNavigate } from 'react-router-dom'
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
